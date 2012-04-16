/*
 * @file kowopenregionsettings.cpp
 * @brief Handler for OpenRegionInfo event queue message.
 *
 * Copyright (c) 2010, Patrick Sapinski
 *
 * The source code in this file ("Source Code") is provided to you
 * under the terms of the GNU General Public License, version 2.0
 * ("GPL"). Terms of the GPL can be found in doc/GPL-license.txt in
 * this distribution, or online at
 * http://secondlifegrid.net/programs/open_source/licensing/gplv2
 *
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 *
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 *
 * ALL SOURCE CODE IS PROVIDED "AS IS." THE AUTHOR MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 */

#include "llviewerprecompiledheaders.h"
#include "llhttpnode.h"
#include "hippolimits.h"
#include "llfloatertools.h"
#include "llviewercontrol.h"
#include "llagent.h"
#include "llsurface.h"
#include "llviewerregion.h"
#include "llviewerobject.h"
#include "llcallbacklist.h"

#include "linden_common.h"
#include "llwaterparammanager.h"
#include "llwaterparamset.h"
#include "llwlparammanager.h"
#include "llwlparamset.h"
#include "message.h"
#include "meta7windlight.h"
#include "llwindlightsettingresponder.h"

class SetEnvironment : public LLHTTPNode
{
	/*virtual*/ void post(
		LLHTTPNode::ResponsePtr response,
		const LLSD& context,
		const LLSD& content) const
	{
		if( 4 != content.size() ) return;//accept 3 leave out water?
		LLSD::array_const_iterator it = content.beginArray();
		LLSD message = *it++;
		LLSD day_cycle = *it++;
		LLSD skys = *it++;
		LLSD water = *it;
		LLUUID region_id = message["regionID"];
		LLViewerRegion* region = gAgent.getRegion();
		if( region_id != region->getRegionID() )
		{
			llwarns << "wrong region" << llendl;
			return;
		}
		llwarns << "set water" << llendl;
		//LLWaterParamManager* water_mgr = LLWaterParamManager::instance();
		std::string water_name = "Region Water (";
		water_name.append(region->getName());
		water_name.append(")");
		//water_mgr->loadPresetFromRegion(water_name, water, true);
		//LLWLParamManager* wl_mgr = LLWLParamManager::instance();
		LLSD::map_const_iterator sky_it = skys.beginMap();
		LLSD::map_const_iterator sky_end = skys.endMap();
		for(;sky_it != sky_end; sky_it++)
		{
			wl_mgr->loadPresetFromRegion(sky_it->first, sky_it->second, true);
		}
		std::string dayCycle_name = "Region Settings (";
		wl_mgr->mDay.loadRegionDayCycle(day_cycle);
		dayCycle_name.append(region->getName());
		dayCycle_name.append(")");
		wl_mgr->mDay.mName = dayCycle_name;

		if( 5 == content.size() )//Server sent the time too, so use it
			wl_mgr->resetAnimator(*++it, true);
		else
			wl_mgr->resetAnimator(0.5, true);//Set as midday then
		wl_mgr->mAnimator.mUseLindenTime = true;
	}
	};

	LLHTTPRegistration<SetEnvironment>
	gHTTPRegistrationWindLightSettingsUpdate(
		"/message/SetEnvironment");

	class EnvironmentSettingsResponder : public LLHTTPClient::Responder
	{
public:

  virtual void result(const LLSD& content)
  {
    if( 4 != content.size() ) return;//accept 3 leave out water?
    LLSD::array_const_iterator it = content.beginArray();
    LLSD message = *it++;
    LLSD day_cycle = *it++;
    LLSD skys = *it++;
    LLSD water = *it;
    LLUUID region_id = message["regionID"];
    LLViewerRegion* region = gAgent.getRegion();
    if( region_id != region->getRegionID() )
    {
        llwarns << "wrong region" << llendl;
        return;
    }
    llwarns << "set water" << llendl;
    LLWaterParamManager* water_mgr = LLWaterParamManager::instance();
    std::string water_name = "Region Water (";
    water_name.append(region->getName());
    water_name.append(")");
    water_mgr->loadPresetFromRegion(water_name, water, true);
    LLWLParamManager* wl_mgr = LLWLParamManager::instance();
    LLSD::map_const_iterator sky_it = skys.beginMap();
    LLSD::map_const_iterator sky_end = skys.endMap();
    for(;sky_it != sky_end; sky_it++)
    {
        wl_mgr->loadPresetFromRegion(sky_it->first, sky_it->second, true);
    }
	std::string dayCycle_name = "Region Settings (";
    wl_mgr->mDay.loadRegionDayCycle(day_cycle);
    dayCycle_name.append(region->getName());
    dayCycle_name.append(")");
    wl_mgr->mDay.mName = dayCycle_name;

	if( 5 == content.size() )//Server sent the time too, so use it
		wl_mgr->resetAnimator(*++it, true);
	else
		wl_mgr->resetAnimator(0.5, true);//Set as midday then
    wl_mgr->mAnimator.mUseLindenTime = true;
  }

  virtual void error(U32 status, const std::string& reason)
  {
    llwarns << "EnvironmentSettings::error("
    << status << ": " << reason << ")" << llendl;
  }
};

IMPEnvironmentSettings::IMPEnvironmentSettings()
{
  mRegionID.setNull();
}

void IMPEnvironmentSettings::init()
{
  gIdleCallbacks.addFunction(idle, this);
}

void IMPEnvironmentSettings::idle(void* user_data)
{
  IMPEnvironmentSettings* self = (IMPEnvironmentSettings*)user_data;

  LLViewerRegion* region = gAgent.getRegion();
  if(region && region->capabilitiesReceived())
  {
    LLUUID region_id = region->getRegionID();
    if( region_id != self->mRegionID)
    {
      self->mRegionID = region_id;
      self->getEnvironmentSettings();
    }
  }
}

void IMPEnvironmentSettings::getEnvironmentSettings()
{
  if(gSavedSettings.getBOOL("UseServersideWindlightSettings"))
  {
	  std::string url = gAgent.getRegion()->getCapability("EnvironmentSettings");
	  if (!url.empty())
	  {
		 LLHTTPClient::get(url, new EnvironmentSettingsResponder);
	  }
  }
}