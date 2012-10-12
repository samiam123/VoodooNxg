/**
 * @file aistatemachine.cpp
 * @brief Implementation of AIStateMachine
 *
 * Copyright (c) 2010, Aleric Inglewood.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution.
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   01/03/2010
 *   Initial version, written by Aleric Inglewood @ SL
 */

#include "linden_common.h"

#include <algorithm>

#include "llcallbacklist.h"
#include "llcontrol.h"
#include "llfasttimer.h"
#include "aithreadsafe.h"
#include "aistatemachine.h"

extern F64 calc_clock_frequency(void);

extern LLControlGroup gSavedSettings;

// Local variables.
namespace {
    struct QueueElementComp;

    class QueueElement {
    private:
        AIStateMachine* mStateMachine;
        U64 mRuntime;

    public:
        QueueElement(AIStateMachine* statemachine) : mStateMachine(statemachine), mRuntime(0) { }
        friend bool operator==(QueueElement const& e1, QueueElement const& e2) { return e1.mStateMachine == e2.mStateMachine; }
        friend struct QueueElementComp;

        AIStateMachine& statemachine(void) const { return *mStateMachine; }
        void add(U64 count) { mRuntime += count; }
    };

    struct QueueElementComp {
        bool operator()(QueueElement const& e1, QueueElement const& e2) const { return e1.mRuntime < e2.mRuntime; }
    };

    typedef std::vector<QueueElement> active_statemachines_type;
    active_statemachines_type active_statemachines;
    typedef std::vector<AIStateMachine*> continued_statemachines_type;
    struct cscm_type
    {
        continued_statemachines_type continued_statemachines;
        bool calling_mainloop;
    };
    AIThreadSafeDC<cscm_type> continued_statemachines_and_calling_mainloop;
}

// static
AIThreadSafeSimpleDC<U64> AIStateMachine::sMaxCount;

void AIStateMachine::updateSettings(void)
{
    Dout(dc::statemachine, "Initializing AIStateMachine::sMaxCount");
    *AIAccess<U64>(sMaxCount) = calc_clock_frequency() * gSavedSettings.getU32("StateMachineMaxTime") / 1000;
}

//----------------------------------------------------------------------------
//
// Public methods
//

void AIStateMachine::run(AIStateMachine* parent, state_type new_parent_state, bool abort_parent)
{
    DoutEntering(dc::statemachine, "AIStateMachine::run(" << (void*)parent << ", " << (parent ? parent->state_str(new_parent_state) : "NA") << ", " << abort_parent << ") [" << (void*)this << "]");
    // Must be the first time we're being run, or we must be called from a callback function.
    llassert(!mParent || mState == bs_callback);
    llassert(!mCallback || mState == bs_callback);
    // Can only be run when in this state.
    llassert(mState == bs_initialize || mState == bs_callback);

    // Allow NULL to be passed as parent to signal that we want to reuse the old one.
    if (parent)
    {
        mParent = parent;
        // In that case remove any old callback!
        if (mCallback)
        {
            delete mCallback;
            mCallback = NULL;
        }

        mNewParentState = new_parent_state;
        mAbortParent = abort_parent;
    }

    // If abort_parent is requested then a parent must be provided.
    llassert(!abort_parent || mParent);
    // If a parent is provided, it must be running.
    llassert(!mParent || mParent->mState == bs_run);

    // Mark that run() has been called, in case we're being called from a callback function.
    mState = bs_initialize;

    cont();
}

void AIStateMachine::run(callback_type::signal_type::slot_type const& slot)
{
    DoutEntering(dc::statemachine, "AIStateMachine::run(<slot>) [" << (void*)this << "]");
    // Must be the first time we're being run, or we must be called from a callback function.
    llassert(!mParent || mState == bs_callback);
    llassert(!mCallback || mState == bs_callback);
    // Can only be run when in this state.
    llassert(mState == bs_initialize || mState == bs_callback);

    // Clean up any old callbacks.
    mParent = NULL;
    if (mCallback)
    {
        delete mCallback;
        mCallback = NULL;
    }

    mCallback = new callback_type(slot);

    // Mark that run() has been called, in case we're being called from a callback function.
    mState = bs_initialize;

    cont();
}

void AIStateMachine::idle(void)
{
    DoutEntering(dc::statemachine, "AIStateMachine::idle() [" << (void*)this << "]");
    llassert(!mIdle);
    mIdle = true;
    mSleep = 0;
}

// About thread safeness:
//
// The main thread initializes a statemachine and calls run, so a statemachine
// runs in the main thread. However, it is allowed that a state calls idle()
// and then allows one and only one other thread to call cont() upon some
// event (only once, of course, as idle() has to be called before cont()
// can be called again-- and another thread is not allowed to call idle()).
// Instead of cont(), the other thread may also call set_state().

void AIStateMachine::cont(void)
{
    DoutEntering(dc::statemachine, "AIStateMachine::cont() [" << (void*)this << "]");
    llassert(mIdle);
    // Atomic test mActive and change mIdle.
    mIdleActive.lock();
    mIdle = false;
    bool not_active = mActive == as_idle;
    mIdleActive.unlock();
    if (not_active)
    {
        AIWriteAccess<cscm_type> cscm_w(continued_statemachines_and_calling_mainloop);
        // We only get here when the statemachine was idle (set by the main thread),
        // see first assertion. Hence, the main thread is not changing this, as the
        // statemachine is not running. Thus, mActive can have changed when a THIRD
        // thread called cont(), which is not allowed: if two threads can call cont()
        // at any moment then the first assertion can't hold.
        llassert_always(mActive == as_idle);
        cscm_w->continued_statemachines.push_back(this);
        if (!cscm_w->calling_mainloop)
        {
            Dout(dc::statemachine, "Adding AIStateMachine::mainloop to gIdleCallbacks");
            cscm_w->calling_mainloop = true;
            gIdleCallbacks.addFunction(&AIStateMachine::mainloop);
        }
        mActive = as_queued;
        llassert_always(!mIdle);	// It should never happen that one thread calls cont() while another calls idle() concurrently.
    }
}

void AIStateMachine::set_state(state_type state)
{
    DoutEntering(dc::statemachine, "AIStateMachine::set_state(" << state_str(state) << ") [" << (void*)this << "]");
    llassert(mState == bs_run);
    if (mRunState != state)
    {
        mRunState = state;
        Dout(dc::statemachine, "mRunState set to " << state_str(mRunState));
    }
    if (mIdle)
        cont();
}

void AIStateMachine::abort(void)
{
    DoutEntering(dc::statemachine, "AIStateMachine::abort() [" << (void*)this << "]");
    llassert(mState == bs_run);
    mState = bs_abort;
    abort_impl();
    mAborted = true;
    finish();
}

void AIStateMachine::finish(void)
{
    DoutEntering(dc::statemachine, "AIStateMachine::finish() [" << (void*)this << "]");
    llassert(mState == bs_run || mState == bs_abort);
    // It is possible that mIdle is true when abort or finish was called from
    // outside multiplex_impl. However, that only may be done by the main thread.
    llassert(!mIdle || is_main_thread());
    if (!mIdle)
        idle();
    mState = bs_finish;
    finish_impl();
    // Did finish_impl call kill()? Then that is only the default. Remember it.
    bool default_delete = (mState == bs_killed);
    mState = bs_finish;
    if (mParent)
    {
        // It is possible that the parent is not running when the parent is in fact aborting and called
        // abort on this object from it's abort_impl function. It that case we don't want to recursively
        // call abort again (or change it's state).
        if (mParent->running())
        {
            if (mAborted && mAbortParent)
            {
                mParent->abort();
                mParent = NULL;
            }
            else
            {
                mParent->set_state(mNewParentState);
            }
        }
    }
    // After this (bool)*this evaluates to true and we can call the callback, which then is allowed to call run().
    mState = bs_callback;
    if (mCallback)
    {
        // This can/may call kill() that sets mState to bs_kill and in which case the whole AIStateMachine
        // will be deleted from the mainloop, or it may call run() that sets mState is set to bs_initialize
        // and might change or reuse mCallback or mParent.
        mCallback->callback(!mAborted);
        if (mState != bs_initialize)
        {
            delete mCallback;
            mCallback = NULL;
            mParent = NULL;
        }
    }
    else
    {
        // Not restarted by callback. Allow run() to be called later on.
        mParent = NULL;
    }
    // Fix the final state.
    if (mState == bs_callback)
        mState = default_delete ? bs_killed : bs_initialize;
    if (mState == bs_killed && mActive == as_idle)
    {
        // Bump the statemachine onto the active statemachine list, or else it won't be deleted.
        cont();
        idle();
    }
}

void AIStateMachine::kill(void)
{
    // Should only be called from finish() (or when not running (bs_initialize)).
    llassert(mIdle && (mState == bs_callback || mState == bs_finish || mState == bs_initialize));
    base_state_type prev_state = mState;
    mState = bs_killed;
    if (prev_state == bs_initialize)
    {
        // We're not running (ie being deleted by a parent statemachine), delete it immediately.
        delete this;
    }
}

// Return stringified 'state'.
char const* AIStateMachine::state_str(state_type state)
{
    if (state >= min_state && state < max_state)
    {
        switch (state)
        {
        AI_CASE_RETURN(bs_initialize);
        AI_CASE_RETURN(bs_run);
        AI_CASE_RETURN(bs_abort);
        AI_CASE_RETURN(bs_finish);
        AI_CASE_RETURN(bs_callback);
        AI_CASE_RETURN(bs_killed);
        }
    }
    return state_str_impl(state);
}

//----------------------------------------------------------------------------
//
// Private methods
//

void AIStateMachine::multiplex(U64 current_time)
{
    // Return immediately when this state machine is sleeping.
    // A negative value of mSleep means we're counting frames,
    // a positive value means we're waiting till a certain
    // amount of time has passed.
    if (mSleep != 0)
    {
        if (mSleep < 0)
        {
            if (++mSleep)
                return;
        }
        else
        {
            if (current_time < (U64)mSleep)
                return;
            mSleep = 0;
        }
    }

    DoutEntering(dc::statemachine, "AIStateMachine::multiplex() [" << (void*)this << "] [with state: " << state_str(mState == bs_run ? mRunState : mState) << "]");
    llassert(mState == bs_initialize || mState == bs_run);

    // Real state machine starts here.
    if (mState == bs_initialize)
    {
        mAborted = false;
        mState = bs_run;
        initialize_impl();
        if (mAborted || mState != bs_run)
            return;
    }
    multiplex_impl();
}

static LLFastTimer::DeclareTimer FTM_STATEMACHINE("State Machine");
// static
void AIStateMachine::mainloop(void*)
{
    LLFastTimer t(FTM_STATEMACHINE);
    // Add continued state machines.
    {
        AIReadAccess<cscm_type> cscm_r(continued_statemachines_and_calling_mainloop);
        bool nonempty = false;
        for (continued_statemachines_type::const_iterator iter = cscm_r->continued_statemachines.begin(); iter != cscm_r->continued_statemachines.end(); ++iter)
        {
            nonempty = true;
            active_statemachines.push_back(QueueElement(*iter));
            Dout(dc::statemachine, "Adding " << (void*)*iter << " to active_statemachines");
            (*iter)->mActive = as_active;
        }
        if (nonempty)
            AIWriteAccess<cscm_type>(cscm_r)->continued_statemachines.clear();
    }
    llassert(!active_statemachines.empty());
    // Run one or more state machines.
    U64 total_clocks = 0;
    U64 max_count = *AIAccess<U64>(sMaxCount);
    for (active_statemachines_type::iterator iter = active_statemachines.begin(); iter != active_statemachines.end(); ++iter)
    {
        AIStateMachine& statemachine(iter->statemachine());
        if (!statemachine.mIdle)
        {
            U64 start = get_clock_count();
            // This might call idle() and then pass the statemachine to another thread who then may call cont().
            // Hence, after this isn't not sure what mIdle is, and it can change from true to false at any moment,
            // if it is true after this function returns.
            iter->statemachine().multiplex(start);
            U64 delta = get_clock_count() - start;
            iter->add(delta);
            total_clocks += delta;
            if (total_clocks >= max_count)
            {
#ifndef LL_RELEASE_FOR_DOWNLOAD
                llwarns << "AIStateMachine::mainloop did run for " << (total_clocks * 1000 / calc_clock_frequency()) << " ms." << llendl;
#endif
				std::sort(active_statemachines.begin(), active_statemachines.end(), QueueElementComp());
				break;
			}
		}
	}
	// Remove idle state machines from the loop.
	active_statemachines_type::iterator iter = active_statemachines.begin();
	while (iter != active_statemachines.end())
	{
		AIStateMachine& statemachine(iter->statemachine());
		// Atomic test mIdle and change mActive.
		bool locked = statemachine.mIdleActive.tryLock();
		// If the lock failed, then another thread is in the middle of calling cont(),
		// thus mIdle will end up false. So, there is no reason to block here; just
		// treat mIdle as false already.
		if (locked && statemachine.mIdle)
		{
			// Without the lock, it would be possible that another thread called cont() right here,
			// changing mIdle to false again but NOT adding the statemachine to continued_statemachines,
			// thinking it is in active_statemachines (and it is), while immediately below it is
			// erased from active_statemachines.
			statemachine.mActive = as_idle;
			// Now, calling cont() is ok -- as that will cause the statemachine to be added to
			// continued_statemachines, so it's fine in that case-- even necessary-- to remove it from
			// active_statemachines regardless, and we can release the lock here.
			statemachine.mIdleActive.unlock();
			Dout(dc::statemachine, "Erasing " << (void*)&statemachine << " from active_statemachines");
			iter = active_statemachines.erase(iter);
			if (statemachine.mState == bs_killed)
			{
				Dout(dc::statemachine, "Deleting " << (void*)&statemachine);
				delete &statemachine;
			}
		}
		else
		{
			if (locked)
			{
				statemachine.mIdleActive.unlock();
			}
			llassert(statemachine.mActive == as_active);	// It should not be possible that another thread called cont() and changed this when we are we are not idle.
			llassert(statemachine.mState == bs_run || statemachine.mState == bs_initialize);
			++iter;
		}
	}
	if (active_statemachines.empty())
	{
		// If this was the last state machine, remove mainloop from the IdleCallbacks.
		AIReadAccess<cscm_type> cscm_r(continued_statemachines_and_calling_mainloop);
		if (cscm_r->continued_statemachines.empty() && cscm_r->calling_mainloop)
		{
			Dout(dc::statemachine, "Removing AIStateMachine::mainloop from gIdleCallbacks");
			AIWriteAccess<cscm_type>(cscm_r)->calling_mainloop = false;
			gIdleCallbacks.deleteFunction(&AIStateMachine::mainloop);
		}
	}
}
