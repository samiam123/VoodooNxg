/**
 * @file llfloateravatarlist.cpp
 * @brief Avatar list/radar floater
 *
 * @author Dale Glass <dale@daleglass.net>, (C) 2007
 */

/**
 * Rewritten by jcool410
 * Removed usage of globals
 * Removed TrustNET
 * Added utilization of "minimap" data
 * Heavily modified by Henri Beauchamp (the laggy spying tool becomes a true,
 * low lag radar)
 */

#include "llviewerprecompiledheaders.h"

#include "llavatarconstants.h"
#include "llavatarnamecache.h"
#include "llfloateravatarlist.h"

#include "lluictrlfactory.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llscrolllistctrl.h"
#include "llradiogroup.h"
#include "llviewercontrol.h"
#include "llnotificationsutil.h"

#include "llvoavatar.h"
#include "llimview.h"
#include "llfloateravatarinfo.h"
#include "llregionflags.h"
#include "llfloaterreporter.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llfloaterregioninfo.h"
#include "llviewerregion.h"
#include "lltracker.h"
#include "llviewerstats.h"
#include "llerror.h"
#include "llchat.h"
#include "llfloaterchat.h"
#include "llviewermessage.h"
#include "llweb.h"
#include "llviewerobjectlist.h"
#include "llmutelist.h"
#include "llcallbacklist.h"

#include <time.h>
#include <string.h>

#include <map>


#include "llworld.h"
#include "llsdutil.h"
#include "llaudioengine.h"
#include "llstartup.h"
#include "llviewermenu.h"

#include "hippogridmanager.h"

// [RLVa:KB]
#include "rlvhandler.h"
// [/RLVa:KB]

/**
 * @brief How long to keep people who are gone in the list and in memory.
 */
const F32 DEAD_KEEP_TIME = 10.0f;

extern U32 gFrameCount;

typedef enum e_radar_alert_type
{
	ALERT_TYPE_SIM = 1,
	ALERT_TYPE_DRAW = 2,
	ALERT_TYPE_SHOUTRANGE = 4,
	ALERT_TYPE_CHATRANGE = 8,
} ERadarAlertType;

void chat_avatar_status(std::string name, LLUUID key, ERadarAlertType type, bool entering)
{
	static LLCachedControl<bool> radar_chat_alerts(gSavedSettings, "RadarChatAlerts");
	if (!radar_chat_alerts) return;
	static LLCachedControl<bool> radar_alert_sim(gSavedSettings, "RadarAlertSim");
	static LLCachedControl<bool> radar_alert_draw(gSavedSettings, "RadarAlertDraw");
	static LLCachedControl<bool> radar_alert_shout_range(gSavedSettings, "RadarAlertShoutRange");
	static LLCachedControl<bool> radar_alert_chat_range(gSavedSettings, "RadarAlertChatRange");
	static LLCachedControl<bool> radar_chat_keys(gSavedSettings, "RadarChatKeys");

	LLFloaterAvatarList* self = LLFloaterAvatarList::getInstance();
	LLStringUtil::format_map_t args;
	switch(type)
	{
		case ALERT_TYPE_SIM:
			if (radar_alert_sim)
			{
				args["[RANGE]"] = self->getString("the_sim");
			}
			break;

		case ALERT_TYPE_DRAW:
			if (radar_alert_draw)
			{
				args["[RANGE]"] = self->getString("draw_distance");
			}
			break;

		case ALERT_TYPE_SHOUTRANGE:
			if (radar_alert_shout_range)
			{
				args["[RANGE]"] = self->getString("shout_range");
			}
			break;

		case ALERT_TYPE_CHATRANGE:
			if (radar_alert_chat_range)
			{
				args["[RANGE]"] = self->getString("chat_range");
			}
			break;
	}
	if (args.find("[RANGE]") != args.end())
	{
		args["[NAME]"] = name;
		args["[ACTION]"] = self->getString(entering ? "has_entered" : "has_left");
		LLChat chat;
		chat.mText = self->getString("template", args);
		chat.mFromName = name;
		chat.mURL = llformat("secondlife:///app/agent/%s/about",key.asString().c_str());
		chat.mSourceType = CHAT_SOURCE_SYSTEM;
		LLFloaterChat::addChat(chat);
	}
}

LLAvatarListEntry::LLAvatarListEntry(const LLUUID& id, const std::string &name, const LLVector3d &position) :
		mID(id), mName(name), mPosition(position), mDrawPosition(), mMarked(FALSE), mFocused(FALSE),
		mUpdateTimer(), mFrame(gFrameCount), mInSimFrame(U32_MAX), mInDrawFrame(U32_MAX),
		mInChatFrame(U32_MAX), mInShoutFrame(U32_MAX),
		mActivityType(ACTIVITY_NEW), mActivityTimer()
{
}

void LLAvatarListEntry::setPosition(LLVector3d position, bool this_sim, bool drawn, bool chatrange, bool shoutrange)
{
	if (drawn)
	{
		mDrawPosition = position;
	}
	else if (mInDrawFrame == U32_MAX)
	{
		mDrawPosition.setZero();
	}

	mPosition = position;

	mFrame = gFrameCount;
	if (this_sim)
	{
		if (mInSimFrame == U32_MAX)
		{
			chat_avatar_status(mName, mID, ALERT_TYPE_SIM, true);
		}
		mInSimFrame = mFrame;
	}
	if (drawn)
	{
		if (mInDrawFrame == U32_MAX)
		{
			chat_avatar_status(mName, mID, ALERT_TYPE_DRAW, true);
		}
		mInDrawFrame = mFrame;
	}
	if (shoutrange)
	{
		if (mInShoutFrame == U32_MAX)
		{
			chat_avatar_status(mName, mID, ALERT_TYPE_SHOUTRANGE, true);
		}
		mInShoutFrame = mFrame;
	}
	if (chatrange)
	{
		if (mInChatFrame == U32_MAX)
		{
			chat_avatar_status(mName, mID, ALERT_TYPE_CHATRANGE, true);
		}
		mInChatFrame = mFrame;
	}

	mUpdateTimer.start();
}

bool LLAvatarListEntry::getAlive()
{
	U32 current = gFrameCount;
	if (mInSimFrame != U32_MAX && (current - mInSimFrame) >= 2)
	{
		mInSimFrame = U32_MAX;
		chat_avatar_status(mName, mID, ALERT_TYPE_SIM, false);
	}
	if (mInDrawFrame != U32_MAX && (current - mInDrawFrame) >= 2)
	{
		mInDrawFrame = U32_MAX;
		chat_avatar_status(mName, mID, ALERT_TYPE_DRAW, false);
	}
	if (mInShoutFrame != U32_MAX && (current - mInShoutFrame) >= 2)
	{
		mInShoutFrame = U32_MAX;
		chat_avatar_status(mName, mID, ALERT_TYPE_SHOUTRANGE, false);
	}
	if (mInChatFrame != U32_MAX && (current - mInChatFrame) >= 2)
	{
		mInChatFrame = U32_MAX;
		chat_avatar_status(mName, mID, ALERT_TYPE_CHATRANGE, false);
	}
	return ((current - mFrame) <= 2);
}

F32 LLAvatarListEntry::getEntryAgeSeconds()
{
	return mUpdateTimer.getElapsedTimeF32();
}

BOOL LLAvatarListEntry::isDead()
{
	return getEntryAgeSeconds() > DEAD_KEEP_TIME;
}
const F32 ACTIVITY_TIMEOUT = 1.0f;
void LLAvatarListEntry::setActivity(ACTIVITY_TYPE activity)
{
	if ( activity >= mActivityType || mActivityTimer.getElapsedTimeF32() > ACTIVITY_TIMEOUT )
	{
		mActivityType = activity;
		mActivityTimer.start();
	}
}

LLAvatarListEntry::ACTIVITY_TYPE LLAvatarListEntry::getActivity()
{
	if ( mActivityTimer.getElapsedTimeF32() > ACTIVITY_TIMEOUT )
	{
		mActivityType = ACTIVITY_NONE;
	}
	if(isDead())return ACTIVITY_DEAD;

	return mActivityType;
}

LLFloaterAvatarList::LLFloaterAvatarList() :  LLFloater(std::string("radar"))
{
	LLUICtrlFactory::getInstance()->buildFloater(this, "floater_radar.xml");
	mUpdateRate = gSavedSettings.getU32("RadarUpdateRate") * 3 + 3;
}

LLFloaterAvatarList::~LLFloaterAvatarList()
{
	gIdleCallbacks.deleteFunction(LLFloaterAvatarList::callbackIdle);
}

//static
void LLFloaterAvatarList::toggle(void*)
{
// [RLVa:KB]
	if(gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES))
	{
		if(instanceExists())
			getInstance()->close();
	}
	else
// [/RLVa:KB]
	if(!instanceExists() || !getInstance()->getVisible())
	{
		showInstance();
	}
	else
	{
		getInstance()->close();
	}
}

//static
void LLFloaterAvatarList::showInstance()
{
// [RLVa:KB]
	if(gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES))
		return;
// [/RLVa:KB]
	getInstance()->open();
}

void LLFloaterAvatarList::draw()
{
	LLFloater::draw();
}

void LLFloaterAvatarList::onOpen()
{
	gSavedSettings.setBOOL("ShowRadar", TRUE);
}

void LLFloaterAvatarList::onClose(bool app_quitting)
{
	setVisible(FALSE);
	if (!app_quitting)
	{
		gSavedSettings.setBOOL("ShowRadar", FALSE);
	}
	if (!gSavedSettings.getBOOL("RadarKeepOpen") || app_quitting)
	{
		destroy();
	}
}

BOOL LLFloaterAvatarList::postBuild()
{
	// Default values
	mTracking = FALSE;
	mUpdate = TRUE;

	// Set callbacks
	childSetAction("profile_btn", onClickProfile, this);
	childSetAction("im_btn", onClickIM, this);
	childSetAction("offer_btn", onClickTeleportOffer, this);
	childSetAction("track_btn", onClickTrack, this);
	childSetAction("mark_btn", onClickMark, this);
	childSetAction("focus_btn", onClickFocus, this);
	childSetAction("prev_in_list_btn", onClickPrevInList, this);
	childSetAction("next_in_list_btn", onClickNextInList, this);
	childSetAction("prev_marked_btn", onClickPrevMarked, this);
	childSetAction("next_marked_btn", onClickNextMarked, this);

	childSetAction("get_key_btn", onClickGetKey, this);

	childSetAction("freeze_btn", onClickFreeze, this);
	childSetAction("eject_btn", onClickEject, this);
	childSetAction("mute_btn", onClickMute, this);
	childSetAction("ar_btn", onClickAR, this);
	childSetAction("teleport_btn", onClickTeleport, this);
	childSetAction("estate_eject_btn", onClickEjectFromEstate, this);
	childSetAction("estate_ban_btn", onClickBanFromEstate, this);

	childSetAction("send_keys_btn", onClickSendKeys, this);

	getChild<LLRadioGroup>("update_rate")->setSelectedIndex(gSavedSettings.getU32("RadarUpdateRate"));
	childSetCommitCallback("update_rate", onCommitUpdateRate, this);

	// Get a pointer to the scroll list from the interface
	mAvatarList = getChild<LLScrollListCtrl>("avatar_list");
	mAvatarList->sortByColumn("distance", TRUE);
	mAvatarList->setCommitOnSelectionChange(TRUE);
	mAvatarList->setCallbackUserData(this);
	mAvatarList->setCommitCallback(onSelectName);
	mAvatarList->setDoubleClickCallback(onClickFocus);
	refreshAvatarList();

	gIdleCallbacks.addFunction(LLFloaterAvatarList::callbackIdle);

	if(gHippoGridManager->getConnectedGrid()->isSecondLife()){
		LLScrollListCtrl* list = getChild<LLScrollListCtrl>("avatar_list");
		list->getColumn(LIST_AVATAR_NAME)->setWidth(0);
		list->getColumn(LIST_CLIENT)->setWidth(0);
		list->getColumn(LIST_CLIENT)->mDynamicWidth = FALSE;
		list->getColumn(LIST_CLIENT)->mRelWidth = 0;
		list->getColumn(LIST_AVATAR_NAME)->mDynamicWidth = TRUE;
		list->getColumn(LIST_AVATAR_NAME)->mRelWidth = -1;
		list->updateLayout();
	}

	return TRUE;
}

void updateParticleActivity(LLDrawable *drawablep)
{
	if (LLFloaterAvatarList::instanceExists())
	{
		LLViewerObject *vobj = drawablep->getVObj();
		if (vobj && vobj->isParticleSource())
		{
			LLUUID id = vobj->mPartSourcep->getOwnerUUID();
			LLAvatarListEntry *ent = LLFloaterAvatarList::getInstance()->getAvatarEntry(id);
			if ( NULL != ent )
			{
				ent->setActivity(LLAvatarListEntry::ACTIVITY_PARTICLES);
			}
		}
	}
}

void LLFloaterAvatarList::updateAvatarList()
{
	//llinfos << "radar refresh: updating map" << llendl;

	// Check whether updates are enabled
	LLCheckboxCtrl* check = getChild<LLCheckboxCtrl>("update_enabled_cb");
	if (check && !check->getValue())
	{
		mUpdate = FALSE;
		refreshTracker();
		return;
	}
	else
	{
		mUpdate = TRUE;
	}
	//moved to pipeline to prevent a crash
	//gPipeline.forAllVisibleDrawables(updateParticleActivity);


	//todo: make this less of a hacked up copypasta from dales 1.18.
	if(gAudiop != NULL)
	{
		LLAudioEngine::source_map::iterator iter;
		for (iter = gAudiop->mAllSources.begin(); iter != gAudiop->mAllSources.end(); ++iter)
		{
			LLAudioSource *sourcep = iter->second;
			LLUUID uuid = sourcep->getOwnerID();
			LLAvatarListEntry *ent = getAvatarEntry(uuid);
			if ( ent )
			{
				ent->setActivity(LLAvatarListEntry::ACTIVITY_SOUND);
			}
		}
	}

	LLVector3d mypos = gAgent.getPositionGlobal();

	{
		std::vector<LLUUID> avatar_ids;
		std::vector<LLUUID> sorted_avatar_ids;
		std::vector<LLVector3d> positions;

		LLWorld::instance().getAvatars(&avatar_ids, &positions, mypos, F32_MAX);

		sorted_avatar_ids = avatar_ids;
		std::sort(sorted_avatar_ids.begin(), sorted_avatar_ids.end());

		for (std::vector<LLCharacter*>::const_iterator iter = LLCharacter::sInstances.begin(); iter != LLCharacter::sInstances.end(); ++iter)
		{
			LLUUID avid = (*iter)->getID();

			if (!std::binary_search(sorted_avatar_ids.begin(), sorted_avatar_ids.end(), avid))
			{
				avatar_ids.push_back(avid);
			}
		}

		size_t i;
		size_t count = avatar_ids.size();

		bool announce = gSavedSettings.getBOOL("RadarChatKeys");
		std::queue<LLUUID> announce_keys;

		for (i = 0; i < count; ++i)
		{
			std::string name;
			std::string first;
			std::string last;
			const LLUUID &avid = avatar_ids[i];

			LLVector3d position;
			LLVOAvatar* avatarp = gObjectList.findAvatar(avid);

			if (avatarp)
			{
				// Skip if avatar is dead(what's that?)
				// or if the avatar is ourselves.
				// or if the avatar is a dummy
				if (avatarp->isDead() || avatarp->isSelf() || avatarp->mIsDummy)
				{
					continue;
				}

				// Get avatar data
				position = gAgent.getPosGlobalFromAgent(avatarp->getCharacterPosition());
				name = avatarp->getFullname();

				// [Ansariel: Display name support]
				LLAvatarName avatar_name;
				if (LLAvatarNameCache::get(avatarp->getID(), &avatar_name))
				{
				    static LLCachedControl<S32> phoenix_name_system("PhoenixNameSystem", 0);
					switch (phoenix_name_system)
					{
						case 0 : name = avatar_name.getLegacyName(); break;
						case 1 : name = (avatar_name.mIsDisplayNameDefault ? avatar_name.mDisplayName : avatar_name.getCompleteName()); break;
						case 2 : name = avatar_name.mDisplayName; break;
						default : name = avatar_name.getLegacyName(); break;
					}

					first = avatar_name.mLegacyFirstName;
					last = avatar_name.mLegacyLastName;
				}
				else continue;
				// [/Ansariel: Display name support]

				//duped for lower section
				if (name.empty() || (name.compare(" ") == 0))// || (name.compare(gCacheName->getDefaultName()) == 0))
				{
					if (!gCacheName->getFullName(avid, name)) //seems redudant with LLAvatarNameCache::get above...
					{
						continue;
					}
				}

				if (avid.isNull())
				{
					//llinfos << "Key empty for avatar " << name << llendl;
					continue;
				}

				if (mAvatars.count(avid) > 0)
				{
					// Avatar already in list, update position
					F32 dist = (F32)(position - mypos).magVec();
					mAvatars[avid].setPosition(position, (avatarp->getRegion() == gAgent.getRegion()), true, dist < 20.0, dist < 100.0);
					if(avatarp->isTyping())mAvatars[avid].setActivity(LLAvatarListEntry::ACTIVITY_TYPING);
				}
				else
				{
					// Avatar not there yet, add it
					LLAvatarListEntry entry(avid, name, position);
					if(announce && avatarp->getRegion() == gAgent.getRegion())
						announce_keys.push(avid);
					mAvatars[avid] = entry;
				}
			}
			else
			{
				if (i < positions.size())
				{
					position = positions[i];
				}
				else
				{
					continue;
				}

				if (!gCacheName->getFullName(avid, name))
				{
					//name = gCacheName->getDefaultName();
					continue; //prevent (Loading...)
				}

				if (mAvatars.count(avid) > 0)
				{
					// Avatar already in list, update position
					F32 dist = (F32)(position - mypos).magVec();
					mAvatars[avid].setPosition(position, gAgent.getRegion()->pointInRegionGlobal(position), false, dist < 20.0, dist < 100.0);
				}
				else
				{
					LLAvatarListEntry entry(avid, name, position);
					if(announce && gAgent.getRegion()->pointInRegionGlobal(position))
						announce_keys.push(avid);
					mAvatars[avid] = entry;
				}
			}
		}
		//let us send the keys in a more timely fashion
		if(announce && !announce_keys.empty())
		{
			std::ostringstream ids;
			int transact_num = (int)gFrameCount;
			int num_ids = 0;
			while(!announce_keys.empty())
			{
				LLUUID id = announce_keys.front();
				announce_keys.pop();

				ids << "," << id.asString();
				++num_ids;

				if(ids.tellp() > 200)
				{
					gMessageSystem->newMessage("ScriptDialogReply");
					gMessageSystem->nextBlock("AgentData");
					gMessageSystem->addUUID("AgentID", gAgent.getID());
					gMessageSystem->addUUID("SessionID", gAgent.getSessionID());
					gMessageSystem->nextBlock("Data");
					gMessageSystem->addUUID("ObjectID", gAgent.getID());
					gMessageSystem->addS32("ChatChannel", -777777777);
					gMessageSystem->addS32("ButtonIndex", 1);
					gMessageSystem->addString("ButtonLabel",llformat("%d,%d", transact_num, num_ids) + ids.str());
					gAgent.sendReliableMessage();

					num_ids = 0;
					ids.seekp(0);
					ids.str("");
				}
			}
			if(num_ids > 0)
			{
				gMessageSystem->newMessage("ScriptDialogReply");
				gMessageSystem->nextBlock("AgentData");
				gMessageSystem->addUUID("AgentID", gAgent.getID());
				gMessageSystem->addUUID("SessionID", gAgent.getSessionID());
				gMessageSystem->nextBlock("Data");
				gMessageSystem->addUUID("ObjectID", gAgent.getID());
				gMessageSystem->addS32("ChatChannel", -777777777);
				gMessageSystem->addS32("ButtonIndex", 1);
				gMessageSystem->addString("ButtonLabel",llformat("%d,%d", transact_num, num_ids) + ids.str());
				gAgent.sendReliableMessage();
			}
		}
	}

//	llinfos << "radar refresh: done" << llendl;

	expireAvatarList();
	refreshAvatarList();
	refreshTracker();
}

void LLFloaterAvatarList::expireAvatarList()
{
//	llinfos << "radar: expiring" << llendl;
	std::map<LLUUID, LLAvatarListEntry>::iterator iter;
	std::queue<LLUUID> delete_queue;
	bool alive;

	for (iter = mAvatars.begin(); iter != mAvatars.end(); iter++)
	{
		LLAvatarListEntry *entry = &iter->second;

		alive = entry->getAlive();

		if (entry->isDead())
		{
			//llinfos << "radar: expiring avatar " << entry->getName() << llendl;
			LLUUID av_id = entry->getID();
			delete_queue.push(av_id);
		}
	}

	while (!delete_queue.empty())
	{
		mAvatars.erase(delete_queue.front());
		delete_queue.pop();
	}
}

/**
 * Redraws the avatar list
 * Only does anything if the avatar list is visible.
 * @author Dale Glass
 */
void LLFloaterAvatarList::refreshAvatarList() 
{
	// Don't update list when interface is hidden
	if (!getVisible()) return;

	// We rebuild the list fully each time it's refreshed
	// The assumption is that it's faster to refill it and sort than
	// to rebuild the whole list.
	LLDynamicArray<LLUUID> selected = mAvatarList->getSelectedIDs();
	S32 scrollpos = mAvatarList->getScrollPos();

	mAvatarList->deleteAllItems();

	LLVector3d mypos = gAgent.getPositionGlobal();
	LLVector3d posagent;
	posagent.setVec(gAgent.getPositionAgent());
	LLVector3d simpos = mypos - posagent;

	std::map<LLUUID, LLAvatarListEntry>::iterator iter;
	for (iter = mAvatars.begin(); iter != mAvatars.end(); iter++)
	{
		LLSD element;
		LLUUID av_id;
		std::string av_name;

		LLAvatarListEntry* entry = &iter->second;

		// Skip if avatar hasn't been around
		if (entry->isDead())
		{
			continue;
		}

		av_id = entry->getID();
		av_name = entry->getName().c_str();

		LLVector3d position = entry->getPosition();
		BOOL UnknownAltitude = false;

		LLVector3d delta = position - mypos;
		F32 distance = (F32)delta.magVec();
		F32 unknownAlt = (gHippoGridManager->getConnectedGrid()->isSecondLife()) ? 1020.f : 0.f;
		if (position.mdV[VZ] == unknownAlt)
		{
			UnknownAltitude = true;
			distance = 9000.0;
		}
		delta.mdV[2] = 0.0f;
		F32 side_distance = (F32)delta.magVec();

		// HACK: Workaround for an apparent bug:
		// sometimes avatar entries get stuck, and are registered
		// by the client as perpetually moving in the same direction.
		// this makes sure they get removed from the visible list eventually

		//jcool410 -- this fucks up seeing dueds thru minimap data > 1024m away, so, lets just say > 2048m to the side is bad
		//aka 8 sims
		if (side_distance > 2048.0f)
		{
			continue;
		}

		if (av_id.isNull())
		{
			//llwarns << "Avatar with null key somehow got into the list!" << llendl;
			continue;
		}

		element["id"] = av_id;

		element["columns"][LIST_MARK]["column"] = "marked";
		element["columns"][LIST_MARK]["type"] = "text";
		if (entry->isMarked())
		{
			element["columns"][LIST_MARK]["value"] = "X";
			element["columns"][LIST_MARK]["color"] = LLColor4::blue.getValue();
			element["columns"][LIST_MARK]["font-style"] = "BOLD";
		}
		else
		{
			element["columns"][LIST_MARK]["value"] = "";
		}

		element["columns"][LIST_AVATAR_NAME]["column"] = "avatar_name";
		element["columns"][LIST_AVATAR_NAME]["type"] = "text";
		element["columns"][LIST_AVATAR_NAME]["value"] = av_name;
		if (entry->isFocused())
		{
			element["columns"][LIST_AVATAR_NAME]["font-style"] = "BOLD";
		}

		//<edit> custom colors for certain types of avatars!
		//Changed a bit so people can modify them in settings. And since they're colors, again it's possibly account-based. Starting to think I need a function just to determine that. - HgB
		//element["columns"][LIST_AVATAR_NAME]["color"] = gColors.getColor( "MapAvatar" ).getValue();
		LLViewerRegion* parent_estate = LLWorld::getInstance()->getRegionFromPosGlobal(entry->getPosition());
		LLUUID estate_owner = LLUUID::null;
		if(parent_estate && parent_estate->isAlive())
		{
			estate_owner = parent_estate->getOwner();
		}

		static const LLCachedControl<LLColor4> unselected_color(gColors, "ScrollUnselectedColor",LLColor4(0.f, 0.f, 0.f, 0.8f));

		static LLCachedControl<LLColor4> sDefaultListText(gColors, "DefaultListText");
		static LLCachedControl<LLColor4> sRadarTextChatRange(gColors, "RadarTextChatRange");
		static LLCachedControl<LLColor4> sRadarTextShoutRange(gColors, "RadarTextShoutRange");
		static LLCachedControl<LLColor4> sRadarTextDrawDist(gColors, "RadarTextDrawDist");
		LLColor4 name_color = sDefaultListText;

		//Lindens are always more Linden than your friend, make that take precedence
		if(LLMuteList::getInstance()->isLinden(av_name))
		{
			static const LLCachedControl<LLColor4> ascent_linden_color("AscentLindenColor",LLColor4(0.f,0.f,1.f,1.f));
			name_color = ascent_linden_color;
		}
		//check if they are an estate owner at their current position
		else if(estate_owner.notNull() && av_id == estate_owner)
		{
			static const LLCachedControl<LLColor4> ascent_estate_owner_color("AscentEstateOwnerColor",LLColor4(1.f,0.6f,1.f,1.f));
			name_color = ascent_estate_owner_color;
		}
		//without these dots, SL would suck.
		else if(is_agent_friend(av_id))
		{
			static const LLCachedControl<LLColor4> ascent_friend_color("AscentFriendColor",LLColor4(1.f,1.f,0.f,1.f));
			name_color = ascent_friend_color;
		}
		//big fat jerkface who is probably a jerk, display them as such.
		else if(LLMuteList::getInstance()->isMuted(av_id))
		{
			static const LLCachedControl<LLColor4> ascent_muted_color("AscentMutedColor",LLColor4(0.7f,0.7f,0.7f,1.f));
			name_color = ascent_muted_color;
		}

		name_color = name_color*0.5f + unselected_color*0.5f;

		element["columns"][LIST_AVATAR_NAME]["color"] = name_color.getValue();

		char temp[32];
		LLColor4 color = sDefaultListText;
		element["columns"][LIST_DISTANCE]["column"] = "distance";
		element["columns"][LIST_DISTANCE]["type"] = "text";
		if (UnknownAltitude)
		{
			strcpy(temp, "?");
			if (entry->isDrawn())
			{
				color = sRadarTextDrawDist;
			}
		}
		else
		{
			if (distance <= 96.0)
			{
				snprintf(temp, sizeof(temp), "%.1f", distance);
				if (distance > 20.0f)
				{
					color = sRadarTextShoutRange;
				}
				else
				{
					color = sRadarTextChatRange;
				}
			}
			else
			{
				if (entry->isDrawn())
				{
					color = sRadarTextDrawDist;
				}
				snprintf(temp, sizeof(temp), "%d", (S32)distance);
			}
		}
		element["columns"][LIST_DISTANCE]["value"] = temp;
		element["columns"][LIST_DISTANCE]["color"] = color.getValue();

		position = position - simpos;

		S32 x = (S32)position.mdV[VX];
		S32 y = (S32)position.mdV[VY];
		if (x >= 0 && x <= 256 && y >= 0 && y <= 256)
		{
			snprintf(temp, sizeof(temp), "%d, %d", x, y);
		}
		else
		{
			temp[0] = '\0';
			if (y < 0)
			{
				strcat(temp, "S");
			}
			else if (y > 256)
			{
				strcat(temp, "N");
			}
			if (x < 0)
			{
				strcat(temp, "W");
			}
			else if (x > 256)
			{
				strcat(temp, "E");
			}
		}
		element["columns"][LIST_POSITION]["column"] = "position";
		element["columns"][LIST_POSITION]["type"] = "text";
		element["columns"][LIST_POSITION]["value"] = temp;

		element["columns"][LIST_ALTITUDE]["column"] = "altitude";
		element["columns"][LIST_ALTITUDE]["type"] = "text";
		if (UnknownAltitude)
		{
			strcpy(temp, "?");
		}
		else
		{
			snprintf(temp, sizeof(temp), "%d", (S32)position.mdV[VZ]);
		}
		element["columns"][LIST_ALTITUDE]["value"] = temp;

		element["columns"][LIST_ACTIVITY]["column"] = "activity";
		element["columns"][LIST_ACTIVITY]["type"] = "icon";

		std::string activity_icon = "";
		switch(entry->getActivity())
		{
		case LLAvatarListEntry::ACTIVITY_MOVING:
			activity_icon = "inv_item_animation.tga";
			break;
		case LLAvatarListEntry::ACTIVITY_GESTURING:
			activity_icon = "inv_item_gesture.tga";
			break;
		case LLAvatarListEntry::ACTIVITY_SOUND:
			activity_icon = "inv_item_sound.tga";
			break;
		case LLAvatarListEntry::ACTIVITY_REZZING:
			activity_icon = "ff_edit_theirs.tga";
			break;
		case LLAvatarListEntry::ACTIVITY_PARTICLES:
			activity_icon = "particles_scan.tga";
			break;
		case LLAvatarListEntry::ACTIVITY_NEW:
			activity_icon = "avatar_new.tga";
			break;
		case LLAvatarListEntry::ACTIVITY_TYPING:
			activity_icon = "avatar_typing.tga";
			break;
		default:
			break;
		}

		element["columns"][LIST_ACTIVITY]["value"] = activity_icon;//icon_image_id; //"icn_active-speakers-dot-lvl0.tga";
		//element["columns"][LIST_AVATAR_ACTIVITY]["color"] = icon_color.getValue();

		element["columns"][LIST_CLIENT]["column"] = "client";
		element["columns"][LIST_CLIENT]["type"] = "text";

		//element["columns"][LIST_METADATA]["column"] = "metadata";
		//element["columns"][LIST_METADATA]["type"] = "text";

		static const LLCachedControl<LLColor4> avatar_name_color(gColors, "AvatarNameColor",LLColor4(0.98f, 0.69f, 0.36f, 1.f));
		LLColor4 client_color(avatar_name_color);
		LLVOAvatar* avatarp = gObjectList.findAvatar(av_id);
		if(avatarp)
		{
			std::string client = SHClientTagMgr::instance().getClientName(avatarp, false);
			SHClientTagMgr::instance().getClientColor(avatarp, false, client_color);
			if(client == "")
			{
				client_color = unselected_color;
				client = "?";
			}
			element["columns"][LIST_CLIENT]["value"] = client.c_str();

			// <dogmode>
			// Don't expose Emerald's metadata.

			//if(avatarp->extraMetadata.length())
			//{
			//	element["columns"][LIST_METADATA]["value"] = avatarp->extraMetadata.c_str();
			//}
		}
		else
		{
			element["columns"][LIST_CLIENT]["value"] = "Out Of Range";
		}
		//Blend to make the color show up better
		client_color = client_color *.5f + unselected_color * .5f;

		element["columns"][LIST_CLIENT]["color"] = client_color.getValue();

		// Add to list
		mAvatarList->addElement(element, ADD_BOTTOM);
	}

	// finish
	mAvatarList->sortItems();
	mAvatarList->selectMultiple(selected);
	mAvatarList->setScrollPos(scrollpos);

//	llinfos << "radar refresh: done" << llendl;

}

// static
void LLFloaterAvatarList::onClickIM(void* userdata)
{
	//llinfos << "LLFloaterFriends::onClickIM()" << llendl;
	LLFloaterAvatarList *self = (LLFloaterAvatarList*)userdata;

	LLDynamicArray<LLUUID> ids = self->mAvatarList->getSelectedIDs();
	if (ids.size() > 0)
	{
		if (ids.size() == 1)
		{
			// Single avatar
			LLUUID agent_id = ids[0];

			// [Ansariel: Display name support]
			LLAvatarName avatar_name;
			if (LLAvatarNameCache::get(agent_id, &avatar_name))
			{
				gIMMgr->setFloaterOpen(TRUE);
				gIMMgr->addSession(LLCacheName::cleanFullName(avatar_name.getLegacyName()),IM_NOTHING_SPECIAL,agent_id);
			}
			// [Ansariel: Display name support]
		}
		else
		{
			// Group IM
			LLUUID session_id;
			session_id.generate();
			gIMMgr->setFloaterOpen(TRUE);
			gIMMgr->addSession("Avatars Conference", IM_SESSION_CONFERENCE_START, ids[0], ids);
		}
	}
}

void LLFloaterAvatarList::onClickTeleportOffer(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;

	LLDynamicArray<LLUUID> ids = self->mAvatarList->getSelectedIDs();
	if (ids.size() > 0)
	{
		handle_lure(ids);
	}
}

void LLFloaterAvatarList::onClickTrack(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;

	LLScrollListItem* item = self->mAvatarList->getFirstSelected();
	if (!item) return;

	LLUUID agent_id = item->getUUID();

	if (self->mTracking && self->mTrackedAvatar == agent_id) {
		LLTracker::stopTracking(NULL);
		self->mTracking = FALSE;
	}
	else
	{
		self->mTracking = TRUE;
		self->mTrackedAvatar = agent_id;
//		trackAvatar only works for friends allowing you to see them on map...
//		LLTracker::trackAvatar(agent_id, self->mAvatars[agent_id].getName());
		std::string name = self->mAvatars[agent_id].getName();
		if (!self->mUpdate)
		{
			name += "\n(last known position)";
		}
		LLTracker::trackLocation(self->mAvatars[agent_id].getPosition(), name, name);
	}
}

void LLFloaterAvatarList::refreshTracker()
{
	if (!mTracking) return;

	if (LLTracker::isTracking(NULL))
	{
		LLVector3d pos = mAvatars[mTrackedAvatar].getPosition();
		if (pos != LLTracker::getTrackedPositionGlobal())
		{
			std::string name = mAvatars[mTrackedAvatar].getName();
			if (!mUpdate)
			{
				name += "\n(last known position)";
			}
			LLTracker::trackLocation(pos, name, name);
		}
	}
	else
	{	// Tracker stopped.
		LLTracker::stopTracking(NULL);
		mTracking = FALSE;
//		llinfos << "Tracking stopped." << llendl;
	}
}

LLAvatarListEntry * LLFloaterAvatarList::getAvatarEntry(LLUUID avatar)
{
	if (avatar.isNull())
	{
		return NULL;
	}

	std::map<LLUUID, LLAvatarListEntry>::iterator iter;

	iter = mAvatars.find(avatar);
	if (iter == mAvatars.end())
	{
		return NULL;
	}

	return &iter->second;	
}

//static
void LLFloaterAvatarList::onClickMark(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	LLDynamicArray<LLUUID> ids = self->mAvatarList->getSelectedIDs();

	for (LLDynamicArray<LLUUID>::iterator itr = ids.begin(); itr != ids.end(); ++itr)
	{
		LLUUID avid = *itr;
		LLAvatarListEntry* entry = self->getAvatarEntry(avid);
		if (entry != NULL)
		{
			entry->toggleMark();
		}
	}
}

BOOL LLFloaterAvatarList::handleKeyHere(KEY key, MASK mask)
{
	LLFloaterAvatarList* self = getInstance();
	LLScrollListItem* item = self->mAvatarList->getFirstSelected();
	if(item)
	{
		LLUUID agent_id = item->getUUID();
		if (( KEY_RETURN == key ) && (MASK_NONE == mask))
		{
			self->mFocusedAvatar = agent_id;
			self->focusOnCurrent();
			return TRUE;
		}
		else if (( KEY_RETURN == key ) && (MASK_CONTROL == mask))
		{
			LLAvatarListEntry* entry = self->getAvatarEntry(agent_id);
			if (entry)
			{
//				llinfos << "Trying to teleport to " << entry->getName() << " at " << entry->getPosition() << llendl;
				gAgent.teleportViaLocation(entry->getPosition());
			}
			return TRUE;
		}
	}

	if (( KEY_RETURN == key ) && (MASK_SHIFT == mask))
	{
		LLDynamicArray<LLUUID> ids = self->mAvatarList->getSelectedIDs();
		if (ids.size() > 0)
		{
			if (ids.size() == 1)
			{
				// Single avatar
				LLUUID agent_id = ids[0];

				// [Ansariel: Display name support]
				LLAvatarName avatar_name;
				if (LLAvatarNameCache::get(agent_id, &avatar_name))
				{
					gIMMgr->setFloaterOpen(TRUE);
					gIMMgr->addSession(LLCacheName::cleanFullName(avatar_name.getLegacyName()),IM_NOTHING_SPECIAL,agent_id);
				}
				// [Ansariel: Display name support]
			}
			else
			{
				// Group IM
				LLUUID session_id;
				session_id.generate();
				gIMMgr->setFloaterOpen(TRUE);
				gIMMgr->addSession("Avatars Conference", IM_SESSION_CONFERENCE_START, ids[0], ids);
			}
		}
	}
	return LLPanel::handleKeyHere(key, mask);
}

void LLFloaterAvatarList::onClickFocus(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;

	LLScrollListItem* item = self->mAvatarList->getFirstSelected();
	if (item)
	{
		self->mFocusedAvatar = item->getUUID();
		self->focusOnCurrent();
	}
}

void LLFloaterAvatarList::removeFocusFromAll()
{
	std::map<LLUUID, LLAvatarListEntry>::iterator iter;

	for (iter = mAvatars.begin(); iter != mAvatars.end(); iter++)
	{
		LLAvatarListEntry* entry = &iter->second;
		entry->setFocus(FALSE);
	}
}

void LLFloaterAvatarList::focusOnCurrent()
{
	std::map<LLUUID, LLAvatarListEntry>::iterator iter;
	LLAvatarListEntry* entry;

	if (mAvatars.size() == 0)
	{
		return;
	}

	for (iter = mAvatars.begin(); iter != mAvatars.end(); iter++)
	{
		entry = &iter->second;

		if (entry->isDead())
			continue;

		if (entry->getID() == mFocusedAvatar)
		{
			removeFocusFromAll();
			entry->setFocus(TRUE);
			gAgentCamera.lookAtObject(mFocusedAvatar, false);
			return;
		}
	}
}

void LLFloaterAvatarList::focusOnPrev(BOOL marked_only)
{
	std::map<LLUUID, LLAvatarListEntry>::iterator iter;
	LLAvatarListEntry* prev = NULL;
	LLAvatarListEntry* entry;

	if (mAvatars.size() == 0)
	{
		return;
	}

	for (iter = mAvatars.begin(); iter != mAvatars.end(); iter++)
	{
		entry = &iter->second;

		if (entry->isDead())
			continue;

		if (prev != NULL && entry->getID() == mFocusedAvatar)
		{
			break;
		}

		if ((!marked_only && entry->isDrawn()) || entry->isMarked())
		{
			prev = entry;
		}
	}

	if (prev != NULL)
	{
		removeFocusFromAll();
		prev->setFocus(TRUE);
		mFocusedAvatar = prev->getID();
		gAgentCamera.lookAtObject(mFocusedAvatar, false);
	}
}

void LLFloaterAvatarList::focusOnNext(BOOL marked_only)
{
	std::map<LLUUID, LLAvatarListEntry>::iterator iter;
	BOOL found = FALSE;
	LLAvatarListEntry* next = NULL;
	LLAvatarListEntry* entry;

	if (mAvatars.size() == 0)
	{
		return;
	}

	for (iter = mAvatars.begin(); iter != mAvatars.end(); iter++)
	{
		entry = &iter->second;

		if (entry->isDead())
			continue;

		if (next == NULL && ((!marked_only && entry->isDrawn()) || entry->isMarked()))
		{
			next = entry;
		}

		if (found && ((!marked_only && entry->isDrawn()) || entry->isMarked()))
		{
			next = entry;
			break;
		}

		if (entry->getID() == mFocusedAvatar)
		{
			found = TRUE;
		} 
	}

	if (next != NULL)
	{
		removeFocusFromAll();
		next->setFocus(TRUE);
		mFocusedAvatar = next->getID();
		gAgentCamera.lookAtObject(mFocusedAvatar, false);
	}
}

/*static*/
void LLFloaterAvatarList::lookAtAvatar(LLUUID &uuid)
{ // twisted laws
	LLViewerObject* voavatar = gObjectList.findObject(uuid);
	if(voavatar && voavatar->isAvatar())
	{
		gAgentCamera.setFocusOnAvatar(FALSE, FALSE);
		gAgentCamera.changeCameraToThirdPerson();
		gAgentCamera.setFocusGlobal(voavatar->getPositionGlobal(),uuid);
		gAgentCamera.setCameraPosAndFocusGlobal(voavatar->getPositionGlobal()
				+ LLVector3d(3.5,1.35,0.75) * voavatar->getRotation(),
												voavatar->getPositionGlobal(),
												uuid );
	}
}

//static
void LLFloaterAvatarList::onClickPrevInList(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	self->focusOnPrev(FALSE);
}

//static
void LLFloaterAvatarList::onClickNextInList(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	self->focusOnNext(FALSE);
}

//static
void LLFloaterAvatarList::onClickPrevMarked(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	self->focusOnPrev(TRUE);
}

//static
void LLFloaterAvatarList::onClickNextMarked(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	self->focusOnNext(TRUE);
}

//static
void LLFloaterAvatarList::onClickGetKey(void *userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	LLScrollListItem* item = self->mAvatarList->getFirstSelected();

	if (NULL == item) return;

	LLUUID agent_id = item->getUUID();

	char buffer[UUID_STR_LENGTH];		/*Flawfinder: ignore*/
	agent_id.toString(buffer);

	gViewerWindow->mWindow->copyTextToClipboard(utf8str_to_wstring(buffer));
}
//static
void LLFloaterAvatarList::sendKeys()
{
	LLViewerRegion* regionp = gAgent.getRegion();
	if(!regionp)return;//ALWAYS VALIDATE DATA
	std::ostringstream ids;
	static int last_transact_num = 0;
	int transact_num = (int)gFrameCount;
	int num_ids = 0;

	if(!gSavedSettings.getBOOL("RadarChatKeys"))
	{
		return;
	}

	if(transact_num > last_transact_num)
	{
		last_transact_num = transact_num;
	}
	else
	{
		//on purpose, avatar IDs on map don't change until the next frame.
		//no need to send more than once a frame.
		return;
	}

	if (!regionp) return; // caused crash if logged out/connection lost
	for (int i = 0; i < regionp->mMapAvatarIDs.count(); i++)
	{
		const LLUUID &id = regionp->mMapAvatarIDs.get(i);

		ids << "," << id.asString();
		++num_ids;


		if(ids.tellp() > 200)
		{
			gMessageSystem->newMessage("ScriptDialogReply");
			gMessageSystem->nextBlock("AgentData");
			gMessageSystem->addUUID("AgentID", gAgent.getID());
			gMessageSystem->addUUID("SessionID", gAgent.getSessionID());
			gMessageSystem->nextBlock("Data");
			gMessageSystem->addUUID("ObjectID", gAgent.getID());
			gMessageSystem->addS32("ChatChannel", -777777777);
			gMessageSystem->addS32("ButtonIndex", 1);
			gMessageSystem->addString("ButtonLabel",llformat("%d,%d", transact_num, num_ids) + ids.str());
			gAgent.sendReliableMessage();

			num_ids = 0;
			ids.seekp(0);
			ids.str("");
		}
	}
	if(num_ids > 0)
	{
		gMessageSystem->newMessage("ScriptDialogReply");
		gMessageSystem->nextBlock("AgentData");
		gMessageSystem->addUUID("AgentID", gAgent.getID());
		gMessageSystem->addUUID("SessionID", gAgent.getSessionID());
		gMessageSystem->nextBlock("Data");
		gMessageSystem->addUUID("ObjectID", gAgent.getID());
		gMessageSystem->addS32("ChatChannel", -777777777);
		gMessageSystem->addS32("ButtonIndex", 1);
		gMessageSystem->addString("ButtonLabel",llformat("%d,%d", transact_num, num_ids) + ids.str());
		gAgent.sendReliableMessage();
	}
}
//static
void LLFloaterAvatarList::sound_trigger_hook(LLMessageSystem* msg,void **)
{
	LLUUID  sound_id,owner_id;
	msg->getUUIDFast(_PREHASH_SoundData, _PREHASH_SoundID, sound_id);
	msg->getUUIDFast(_PREHASH_SoundData, _PREHASH_OwnerID, owner_id);
	if(owner_id == gAgent.getID() && sound_id == LLUUID("76c78607-93f9-f55a-5238-e19b1a181389"))
	{
		//let's ask if they want to turn it on.
		if(gSavedSettings.getBOOL("RadarChatKeys"))
		{
			LLFloaterAvatarList::getInstance()->sendKeys();
		}else
		{
			LLSD args;
			args["MESSAGE"] = "An object owned by you has request the keys from your radar.\nWould you like to enable announcing keys to objects in the sim?";
			LLNotificationsUtil::add("GenericAlertYesCancel", args, LLSD(), onConfirmRadarChatKeys);
		}
	}
}
// static
bool LLFloaterAvatarList::onConfirmRadarChatKeys(const LLSD& notification, const LLSD& response )
{
	S32 option = LLNotification::getSelectedOption(notification, response);
	if(option == 0) // yes
	{
		gSavedSettings.setBOOL("RadarChatKeys",TRUE);
		LLFloaterAvatarList::getInstance()->sendKeys();
	}
	return false;
}
//static
void LLFloaterAvatarList::onClickSendKeys(void *userdata)
{
	LLFloaterAvatarList::getInstance()->sendKeys();
}

static void send_freeze(const LLUUID& avatar_id, bool freeze)
{
	U32 flags = 0x0;
	if (!freeze)
	{
		// unfreeze
		flags |= 0x1;
	}

	LLMessageSystem* msg = gMessageSystem;
	LLVOAvatar* avatarp = gObjectList.findAvatar(avatar_id);

	if (avatarp && avatarp->getRegion())
	{
		msg->newMessage("FreezeUser");
		msg->nextBlock("AgentData");
		msg->addUUID("AgentID", gAgent.getID());
		msg->addUUID("SessionID", gAgent.getSessionID());
		msg->nextBlock("Data");
		msg->addUUID("TargetID", avatar_id);
		msg->addU32("Flags", flags);
		msg->sendReliable( avatarp->getRegion()->getHost());
	}
}

static void send_eject(const LLUUID& avatar_id, bool ban)
{	
	LLMessageSystem* msg = gMessageSystem;
	LLVOAvatar* avatarp = gObjectList.findAvatar(avatar_id);

	if (avatarp && avatarp->getRegion())
	{
		U32 flags = 0x0;
		if (ban)
		{
			// eject and add to ban list
			flags |= 0x1;
		}

		msg->newMessage("EjectUser");
		msg->nextBlock("AgentData");
		msg->addUUID("AgentID", gAgent.getID());
		msg->addUUID("SessionID", gAgent.getSessionID());
		msg->nextBlock("Data");
		msg->addUUID("TargetID", avatar_id);
		msg->addU32("Flags", flags);
		msg->sendReliable(avatarp->getRegion()->getHost());
	}
}

static void send_estate_message(
	const char* request,
	const LLUUID& target)
{

	LLMessageSystem* msg = gMessageSystem;
	LLUUID invoice;

	// This seems to provide an ID so that the sim can say which request it's
	// replying to. I think this can be ignored for now.
	invoice.generate();

	llinfos << "Sending estate request '" << request << "'" << llendl;
	msg->newMessage("EstateOwnerMessage");
	msg->nextBlockFast(_PREHASH_AgentData);
	msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
	msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
	msg->addUUIDFast(_PREHASH_TransactionID, LLUUID::null); //not used
	msg->nextBlock("MethodData");
	msg->addString("Method", request);
	msg->addUUID("Invoice", invoice);

	// Agent id
	msg->nextBlock("ParamList");
	msg->addString("Parameter", gAgent.getID().asString().c_str());

	// Target
	msg->nextBlock("ParamList");
	msg->addString("Parameter", target.asString().c_str());

	msg->sendReliable(gAgent.getRegion()->getHost());
}

static void cmd_freeze(const LLUUID& avatar, const std::string& name)      { send_freeze(avatar, true); }
static void cmd_unfreeze(const LLUUID& avatar, const std::string& name)    { send_freeze(avatar, false); }
static void cmd_eject(const LLUUID& avatar, const std::string& name)       { send_eject(avatar, false); }
static void cmd_ban(const LLUUID& avatar, const std::string& name)         { send_eject(avatar, true); }
static void cmd_profile(const LLUUID& avatar, const std::string& name)     { LLFloaterAvatarInfo::showFromDirectory(avatar); }
static void cmd_estate_eject(const LLUUID& avatar, const std::string& name){ send_estate_message("teleporthomeuser", avatar); }
static void cmd_estate_ban(const LLUUID &avatar, const std::string &name)  { LLPanelEstateInfo::sendEstateAccessDelta(ESTATE_ACCESS_BANNED_AGENT_ADD | ESTATE_ACCESS_ALLOWED_AGENT_REMOVE | ESTATE_ACCESS_NO_REPLY, avatar); }

void LLFloaterAvatarList::doCommand(void (*func)(const LLUUID& avatar, const std::string& name))
{
	LLDynamicArray<LLUUID> ids = mAvatarList->getSelectedIDs();

	for (LLDynamicArray<LLUUID>::iterator itr = ids.begin(); itr != ids.end(); ++itr)
	{
		LLUUID avid = *itr;
		LLAvatarListEntry* entry = getAvatarEntry(avid);
		if (entry != NULL)
		{
			llinfos << "Executing command on " << entry->getName() << llendl;
			func(avid, entry->getName());
		}
	}
}

std::string LLFloaterAvatarList::getSelectedNames(const std::string& separator)
{
	std::string ret = "";

	LLDynamicArray<LLUUID> ids = mAvatarList->getSelectedIDs();
	for (LLDynamicArray<LLUUID>::iterator itr = ids.begin(); itr != ids.end(); ++itr)
	{
		LLUUID avid = *itr;
		LLAvatarListEntry* entry = getAvatarEntry(avid);
		if (entry != NULL)
		{
			if (!ret.empty()) ret += separator;
			ret += entry->getName();
		}
	}

	return ret;
}

std::string LLFloaterAvatarList::getSelectedName()
{
	LLUUID id = getSelectedID();
	LLAvatarListEntry* entry = getAvatarEntry(id);
	if (entry)
	{
		return entry->getName();
	}
	return "";
}

LLUUID LLFloaterAvatarList::getSelectedID()
{
	LLScrollListItem* item = mAvatarList->getFirstSelected();
	if (item) return item->getUUID();
	return LLUUID::null;
}

//static 
void LLFloaterAvatarList::callbackFreeze(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (option == 0)
	{
		getInstance()->doCommand(cmd_freeze);
	}
	else if (option == 1)
	{
		getInstance()->doCommand(cmd_unfreeze);
	}
}

//static 
void LLFloaterAvatarList::callbackEject(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (option == 0)
	{
		getInstance()->doCommand(cmd_eject);
	}
	else if (option == 1)
	{
		getInstance()->doCommand(cmd_ban);
	}
}

//static 
void LLFloaterAvatarList::callbackEjectFromEstate(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (option == 0)
	{
		getInstance()->doCommand(cmd_estate_eject);
	}
}

//static
void LLFloaterAvatarList::callbackBanFromEstate(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (option == 0)
	{
		getInstance()->doCommand(cmd_estate_eject); //Eject first, just in case.
		getInstance()->doCommand(cmd_estate_ban);
	}
}

//static
void LLFloaterAvatarList::callbackIdle(void* userdata)
{
	if (instanceExists())
	{
		// Do not update at every frame: this would be insane!
		if (gFrameCount % getInstance()->mUpdateRate == 0)
		{
			getInstance()->updateAvatarList();
		}
	}
}

void LLFloaterAvatarList::onClickFreeze(void* userdata)
{
	LLSD args;
	LLSD payload;
	args["AVATAR_NAME"] = ((LLFloaterAvatarList*)userdata)->getSelectedNames();
	LLNotificationsUtil::add("FreezeAvatarFullname", args, payload, callbackFreeze);
}

//static
void LLFloaterAvatarList::onClickEject(void* userdata)
{
	LLSD args;
	LLSD payload;
	args["AVATAR_NAME"] = ((LLFloaterAvatarList*)userdata)->getSelectedNames();
	LLNotificationsUtil::add("EjectAvatarFullname", args, payload, callbackEject);
}

//static
void LLFloaterAvatarList::onClickMute(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;

	LLDynamicArray<LLUUID> ids = self->mAvatarList->getSelectedIDs();
	if (ids.size() > 0)
	{
		for (LLDynamicArray<LLUUID>::iterator itr = ids.begin(); itr != ids.end(); ++itr)
		{
			LLUUID agent_id = *itr;

			std::string agent_name;
			if (gCacheName->getFullName(agent_id, agent_name))
			{
				if (LLMuteList::getInstance()->isMuted(agent_id))
				{
					LLMute mute(agent_id, agent_name, LLMute::AGENT);
					LLMuteList::getInstance()->remove(mute);	
				}
				else
				{
					LLMute mute(agent_id, agent_name, LLMute::AGENT);
					LLMuteList::getInstance()->add(mute);
				}
			}
		}
	}
}

//static
void LLFloaterAvatarList::onClickEjectFromEstate(void* userdata)
{
	LLSD args;
	LLSD payload;
	args["EVIL_USER"] = ((LLFloaterAvatarList*)userdata)->getSelectedNames();
	LLNotificationsUtil::add("EstateKickUser", args, payload, callbackEjectFromEstate);
}

//static
void LLFloaterAvatarList::onClickBanFromEstate(void* userdata)
{
	LLSD args;
	LLSD payload;
	args["EVIL_USER"] = ((LLFloaterAvatarList*)userdata)->getSelectedNames();
	LLNotificationsUtil::add("EstateBanUser", args, payload, callbackBanFromEstate);
}

//static
void LLFloaterAvatarList::onClickAR(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	LLScrollListItem* item = self->mAvatarList->getFirstSelected();
	if (item)
	{
		LLUUID agent_id = item->getUUID();
		LLAvatarListEntry* entry = self->getAvatarEntry(agent_id);
		if (entry)
		{
			LLFloaterReporter::showFromObject(entry->getID());
		}
	}
}

// static
void LLFloaterAvatarList::onClickProfile(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	self->doCommand(cmd_profile);
}

//static
void LLFloaterAvatarList::onClickTeleport(void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;
	LLScrollListItem* item = self->mAvatarList->getFirstSelected();
	if (item)
	{
		LLUUID agent_id = item->getUUID();
		LLAvatarListEntry* entry = self->getAvatarEntry(agent_id);
		if (entry)
		{
//			llinfos << "Trying to teleport to " << entry->getName() << " at " << entry->getPosition() << llendl;
			gAgent.teleportViaLocation(entry->getPosition());
		}
	}
}

void LLFloaterAvatarList::onSelectName(LLUICtrl*, void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;

	LLScrollListItem* item = self->mAvatarList->getFirstSelected();
	if (item)
	{
		LLUUID agent_id = item->getUUID();
		LLAvatarListEntry* entry = self->getAvatarEntry(agent_id);
		if (entry)
		{
			BOOL enabled = entry->isDrawn();
			self->childSetEnabled("focus_btn", enabled);
			self->childSetEnabled("prev_in_list_btn", enabled);
			self->childSetEnabled("next_in_list_btn", enabled);
		}
	}
}

void LLFloaterAvatarList::onCommitUpdateRate(LLUICtrl*, void* userdata)
{
	LLFloaterAvatarList* self = (LLFloaterAvatarList*)userdata;

	self->mUpdateRate = gSavedSettings.getU32("RadarUpdateRate") * 3 + 3;
}
