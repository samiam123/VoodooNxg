/**
 * @file aistatemachine.h
 * @brief State machine base class
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

#ifndef AISTATEMACHINE_H
#define AISTATEMACHINE_H

#include "aithreadsafe.h"
#include "lltimer.h"
#include <boost/signals2.hpp>

//!
// A AIStateMachine is a base class that allows derived classes to
// go through asynchronous states, while the code still appears to
// be more or less sequential.
//
// These state machine objects can be reused to build more complex
// objects.
//
// It is important to note that each state has a duality: the object
// can have a state that will cause a corresponding function to be
// called; and often that function will end with changing the state
// again, to signal that it was handled. It is easy to confuse the
// function of a state with the state at the end of the function.
// For example, the state "initialize" could cause the member
// function 'init()' to be called, and at the end one would be
// inclined to set the state to "initialized". However, this is the
// wrong approach: the correct use of state names does reflect the
// functions that will be called, never the function that just was
// called.
//
// Each (derived) class goes through a series of states as follows:
//
//   Creation
//       |
//       v
//     (idle) <----.   		Idle until run() is called.
//       |         |
//   Initialize    |		Calls initialize_impl().
//       |         |
//       | (idle)  |		Idle until cont() or set_state() is called.
//       |  |  ^   |
//       v  v  |   |
//   .-------. |   |
//   |  Run  |_,   |		Call multiplex_impl() until idle(), abort() or finish() is called.
//   '-------'     |
//    |    |       |
//    v    |       |
//  Abort  |       |		Calls abort_impl().
//    |    |       |
//    v    v       |
//    Finish       |		Calls finish_impl() (which may call kill()) or
//    |    |       |		the callback function passed to run(), if any,
//    |    v       |
//    | Callback   |        which may call kill() and/or run().
//    |  |   |     |
//    |  |   `-----'
//    v  v
//  Killed					Delete the statemachine (all statemachines must be allocated with new).
//
// Each state causes corresponding code to be called.
// Finish cleans up whatever is done by Initialize.
// Abort should clean up additional things done by Run.
//
// The Run state is entered by calling run().
//
// While the base class is in the Run state, it is the derived class
// that goes through different states. The state variable of the derived
// class is only valid while the base class is in the state Run.
//
// A derived class can exit the Run state by calling one of two methods:
// abort() in case of failure, or finish() in case of success.
// Respectively these set the state to Abort and Finish.
//
// finish_impl may call kill() for a (default) destruction upon finish.
// Even in that case the callback (passed to run()) may call run() again,
// which overrides the request for a default kill. Or, if finish_impl
// doesn't call kill() the callback may call kill() to request the
// destruction of the state machine object.
//
// State machines are run from the "idle" part of the viewer main loop.
// Often a state machine has nothing to do however. In that case it can
// call the method idle(). This will stop the state machine until
// external code changes it's state (by calling set_state()), or calls
// cont() to continue with the last state.
//
// The methods of the derived class call set_state() to change their
// own state within the bs_run state, or by calling either abort()
// or finish().
//
// Restarting a finished state machine can also be done by calling run(),
// which will cause a re-initialize.
//
// Derived classes should implement the following constants:
//
//   static state_type const min_state = first_state;
//   static state_type const max_state = last_state + 1;
//
// Where first_state should be equal to BaseClass::max_state.
// These should represent the minimum and (one past) the maximum
// values of mRunState.
//
//   virtual void initialize_impl(void)
//
// Initializes the derived class.
//
//   virtual void multiplex_impl(void);
//
// This method should handle mRunState in a switch.
// For example:
//
//   switch(mRunState)
//   {
//     case foo:
//       handle_foo();
//       break;
//     case wait_state:
//       if (still_waiting())
//       {
//         idle();
//         break;
//       }
//       set_state(working);
//       /*fall-through*/
//     case working:
//       do_work();
//       if (failure())
//         abort();
//       break;
//     case etc:
//       etc();
//       finish();
//       break;
//   }
//
// virtual void abort_impl(void);
//
//   A call to this method should bring the object to a state
//   where finish_impl() can be called.
//
// virtual void finish_impl(void);
//
//   Should cleanup whatever init_impl() did, or any of the
//   states of the object where multiplex_impl() calls finish().
//   Call kill() from here to make that the default behavior
//   (state machine is deleted unless the callback calls run()).
//
// virtual char const* state_str_impl(state_type run_state);
//
//   Should return a stringified value of run_state.
//
class AIStateMachine {
	//! The type of mState
	enum base_state_type {
	  bs_initialize,
	  bs_run,
	  bs_abort,
	  bs_finish,
	  bs_callback,
	  bs_killed
	};
	//! The type of mActive
	enum active_type {
	  as_idle,					// State machine is on neither list.
	  as_queued,				// State machine is on continued_statemachines list.
	  as_active					// State machine is on active_statemachines list.
	};

  public:
	typedef U32 state_type;		//!< The type of mRunState

	//! Integral value equal to the state with the lowest value.
	static state_type const min_state = bs_initialize;
	//! Integral value one more than the state with the highest value.
	static state_type const max_state = bs_killed + 1;

  private:
	base_state_type mState;						//!< State of the base class.
	bool mIdle;									//!< True if this state machine is not running.
	bool mAborted;								//!< True after calling abort() and before calling run().
	active_type mActive;						//!< Whether statemachine is idle, queued to be added to the active list, or already on the active list.
	S64 mSleep;									//!< Non-zero while the state machine is sleeping.
	LLMutex mIdleActive;						//!< Used for atomic operations on the pair mIdle / mActive.
#ifdef SHOW_ASSERT
	AIThreadID mContThread;						//!< Thread that last called locked_cont().
	bool mCalledThreadUnsafeIdle;				//!< Set to true when idle() is called.
#endif

	// Callback facilities.
	// From within an other state machine:
	AIStateMachine* mParent;					//!< The parent object that started this state machine, or NULL if there isn't any.
	state_type mNewParentState;					//!< The state at which the parent should continue upon a successful finish.
	bool mAbortParent;							//!< If true, abort parent on abort(). Otherwise continue as normal.
	bool mOnAbortSignalParent;					//!< If true and mAbortParent is false, change state of parent even on abort.
	// From outside a state machine:
	struct callback_type {
	  typedef boost::signals2::signal<void (bool)> signal_type;
	  callback_type(signal_type::slot_type const& slot) { connection = signal.connect(slot); }
	  ~callback_type() { connection.disconnect(); }
	  void callback(bool success) const { signal(success); }
	private:
	  boost::signals2::connection connection;
	  signal_type signal;
	};
	callback_type* mCallback;					//!< Pointer to signal/connection, or NULL when not connected.

	static AIThreadSafeSimpleDC<U64> sMaxCount;	//!< Number of cpu clocks below which we start a new state machine within the same frame.

  protected:
	LLMutex mSetStateLock;						//!< For critical areas in set_state() and locked_cont().

	//! State of the derived class. Only valid if mState == bs_run. Call set_state to change.
	volatile state_type mRunState;

  public:
	//! Create a non-running state machine.
	AIStateMachine(void) : mState(bs_initialize), mIdle(true), mAborted(true), mActive(as_idle), mSleep(0), mParent(NULL), mCallback(NULL)
#ifdef SHOW_ASSERT
		, mContThread(AIThreadID::none), mCalledThreadUnsafeIdle(false)
#endif
		{ updateSettings(); }

  protected:
	//! The user should call 'kill()', not delete a AIStateMachine (derived) directly.
	virtual ~AIStateMachine() { llassert((mState == bs_killed && mActive == as_idle) || mState == bs_initialize); }

  public:
	//! Halt the state machine until cont() is called (not thread-safe).
	void idle(void);

	//! Halt the state machine until cont() is called, provided it is still in 'cur_run_state'.
	void idle(state_type current_run_state);

	//! Temporarily halt the state machine.
	void yield_frame(unsigned int frames) { mSleep = -(S64)frames; }

	//! Temporarily halt the state machine.
	void yield_ms(unsigned int ms) { mSleep = get_clock_count() + calc_clock_frequency() * ms / 1000; }

	//! Continue running after calling idle.
	void cont(void)
	{
		mSetStateLock.lock();
		// Ignore calls to cont() if the statemachine isn't idle. See comments in set_state().
		// Calling cont() twice or after calling set_state(), without first calling idle(), is an error.
		if (mState != bs_run || !mIdle) { llassert(mState != bs_run || !mContThread.equals_current_thread()); mSetStateLock.unlock(); return; }
		locked_cont();
	}
  private:
	void locked_cont(void);

  public:
	//---------------------------------------
	// Changing the state.

	//! Change state to <code>bs_run</code>. May only be called after creation or after returning from finish().
	// If <code>parent</code> is non-NULL, change the parent state machine's state to <code>new_parent_state</code>
	// upon finish, or in the case of an abort and when <code>abort_parent</code> is true, call parent->abort() instead.
	void run(AIStateMachine* parent, state_type new_parent_state, bool abort_parent = true, bool on_abort_signal_parent = true);

	//! Change state to 'bs_run'. May only be called after creation or after returning from finish().
	// Does not cause a callback.
	void run(void) { run(NULL, 0, false); }

	//! The same as above, but pass the result of a boost::bind with _1.
	//
	// Here _1, if present, will be replaced with a bool indicating success.
	//
	// For example:
	//
	// <code>
	// struct Foo { void callback(AIStateMachineDerived* ptr, bool success); };
	// ...
	//   AIStateMachineDerived* magic = new AIStateMachineDerived; // Deleted by callback
	//   // Call foo_ptr->callback(magic, _1) on finish.
	//   state_machine->run(boost::bind(&Foo::callback, foo_ptr, magic, _1));
	// </code>
	//
	// or
	//
	// <code>
	// struct Foo { void callback(bool success, AIStateMachineDerived const& magic); };
	// ...
	//   AIStateMachineDerived magic;
	//   // Call foo_ptr->callback(_1, magic) on finish.
	//   magic.run(boost::bind(&Foo::callback, foo_ptr, _1, magic));
	// </code>
	//
	// or
	//
	// <code>
	// static void callback(void* userdata);
	// ...
	//   AIStateMachineDerived magic;
	//   // Call callback(userdata) on finish.
	//   magic.run(boost::bind(&callback, userdata));
	// </code>
	void run(callback_type::signal_type::slot_type const& slot);

	//! Change state to 'bs_abort'. May only be called while in the bs_run state.
	void abort(void);

	//! Change state to 'bs_finish'. May only be called while in the bs_run state.
	void finish(void);

	//! Refine state while in the bs_run state. May only be called while in the bs_run state.
	void set_state(state_type run_state);

	//! Change state to 'bs_killed'. May only be called while in the bs_finish state.
	void kill(void);

	//---------------------------------------
	// Other.

	//! Called whenever the StateMachineMaxTime setting is changed.
	static void updateSettings(void);

	//---------------------------------------
	// Accessors.

	//! Return true if state machine was aborted (can be used in finish_impl).
	bool aborted(void) const { return mAborted; }

	//! Return true if the derived class is running (also when we are idle).
	bool running(void) const { return mState == bs_run; }

	//! Return true if it's safe to call abort.
	bool abortable(void) const { return mState == bs_run || mState == bs_initialize; }

	//! Return true if the derived class is running but idle.
	bool waiting(void) const { return mState == bs_run && mIdle; }

	// Use some safebool idiom (http://www.artima.com/cppsource/safebool.html) rather than operator bool.
	typedef volatile state_type AIStateMachine::* const bool_type;
	//! Return true if state machine successfully finished.
	operator bool_type() const { return ((mState == bs_initialize || mState == bs_callback) && !mAborted) ? &AIStateMachine::mRunState : 0; }

	//! Return a stringified state, for debugging purposes.
	char const* state_str(state_type state);

  private:
	static void add_continued_statemachines(void);
	static void mainloop(void*);
	void multiplex(U64 current_time);

  public:
	//! Abort all running state machines and then run mainloop until all state machines are idle (called when application is exiting).
	static void flush(void);

  protected:
	//---------------------------------------
	// Derived class implementations.

	// Handle initializing the object.
	virtual void initialize_impl(void) = 0;

	// Handle mRunState.
	virtual void multiplex_impl(void) = 0;

	// Handle aborting from current bs_run state.
	virtual void abort_impl(void) = 0;

	// Handle cleaning up from initialization (or post abort) state.
	virtual void finish_impl(void) = 0;

	// Implemenation of state_str for run states.
	virtual char const* state_str_impl(state_type run_state) const = 0;
};

// This case be used in state_str_impl.
#define AI_CASE_RETURN(x) do { case x: return #x; } while(0)

#endif
