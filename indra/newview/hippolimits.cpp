#include "llviewerprecompiledheaders.h"

#include "hippolimits.h"

#include "hippogridmanager.h"

#include "llerror.h"

#include "llviewercontrol.h"		// gSavedSettings

HippoLimits *gHippoLimits = 0;


HippoLimits::HippoLimits()
{
	setLimits();
}


void HippoLimits::setLimits()
{
	if (gHippoGridManager->getConnectedGrid()->getPlatform() == HippoGridInfo::PLATFORM_SECONDLIFE) {
		setSecondLifeLimits();
	} else if (gHippoGridManager->getConnectedGrid()->getPlatform() == HippoGridInfo::PLATFORM_AURORA) {
		setAuroraLimits();
	} else {
		setOpenSimLimits();
	}
}


void HippoLimits::setOpenSimLimits()
{
	mMaxAgentGroups = gHippoGridManager->getConnectedGrid()->getMaxAgentGroups();
	if (mMaxAgentGroups < 0) mMaxAgentGroups = 50;
	mMaxPrimScale = 256.0f;
	mMinPrimXPos = 0;
	mMinPrimYPos = 0;
	mMinPrimZPos = 0;
	mMaxPrimXPos = F32_MAX;
	mMaxPrimYPos = F32_MAX;
	mMaxPrimZPos = F32_MAX;
	mMaxHeight = 10000.0f;
	mMaxLinkedPrims = 255;
	mMaxPhysLinkedPrims = 38;
	//mMaxPhysLinkedPrims = S32_MAX;
	mAllowParcelWindLight = TRUE;
	mAllowMinimap = TRUE;
	mMaxInventoryItemsTransfer = 68;
	mRenderName = 2;
	mAllowPhysicalPrims = TRUE;
	skyUseClassicClouds = TRUE;
	mEnableTeenMode = FALSE;
	mEnforceMaxBuild = FALSE;
	mRenderWater = FALSE;
	mVoiceConnector = "SLVoice";
	mMaxSelectDistance = 192.0f;
	mTerrainScale = 16.0f;
	mDrawDistance = 128;
	mLockedDrawDistance = FALSE;
	if (gHippoGridManager->getConnectedGrid()->isRenderCompat()) {
		llinfos << "Using rendering compatible OpenSim limits." << llendl;
		mMinPrimScale = 0.01f;
		mMinHoleSize = 0.05f;
		mMaxHollow = 0.95f;
	} else {
		llinfos << "Using Hippo OpenSim limits." << llendl;
		mMinPrimScale = 0.001f;
		mMinHoleSize = 0.01f;
		mMaxHollow = 0.99f;
	}
}

void HippoLimits::setAuroraLimits()
{
	mMaxAgentGroups = gHippoGridManager->getConnectedGrid()->getMaxAgentGroups();
	if (mMaxAgentGroups < 0) mMaxAgentGroups = 50;
	mMaxPrimScale = 256.0f;
	mMinPrimScale = 0.001f;
	mMaxPhysPrimScale = 60;//added for openregion
	mMinPrimXPos = 0;
	mMinPrimYPos = 0;
	mMinPrimZPos = 0;
	mMaxPrimXPos = F32_MAX;
	mMaxPrimYPos = F32_MAX;
	mMaxPrimZPos = F32_MAX;
	mMaxHeight = 8192.0f;
	mMaxLinkedPrims = 1000;
	mMaxPhysLinkedPrims = 38;
	mAllowParcelWindLight = TRUE;
	mAllowMinimap = TRUE;
	mMaxInventoryItemsTransfer = 68;
	mRenderName = 2;
	mAllowPhysicalPrims = TRUE;
	skyUseClassicClouds = TRUE;
	mEnableTeenMode = FALSE;
	mEnforceMaxBuild = FALSE;
	mRenderWater = TRUE;
	mVoiceConnector = "SLVoice";
	mMaxSelectDistance = 256.0f;
	mDrawDistance = 128;
	mLockedDrawDistance = FALSE;
	mMinHoleSize = 0.001f;
	mMaxHollow = 99.0f;
}

void HippoLimits::setSecondLifeLimits()
{
	llinfos << "Using Second Life limits." << llendl;
	
	if (gHippoGridManager->getConnectedGrid())
	
	//KC: new server defined max groups
	mMaxAgentGroups = gHippoGridManager->getConnectedGrid()->getMaxAgentGroups();
	if (mMaxAgentGroups <= 0)
	{
		mMaxAgentGroups = DEFAULT_MAX_AGENT_GROUPS;
	}

	mMinPrimScale = 0.01f;
	mMinPrimXPos = 0;
	mMinPrimYPos = 0;
	mMinPrimZPos = 0;
	mMaxPrimXPos = F32_MAX;
	mMaxPrimYPos = F32_MAX;
	mMaxPrimZPos = F32_MAX;
	mMaxHeight = 4096.0f;
	mMaxLinkedPrims = 1000;
	mMaxPhysLinkedPrims = 40;
	mMaxPhysLinkedPrims = S32_MAX;
	mAllowParcelWindLight = TRUE;
	mAllowMinimap = TRUE;
	mMaxInventoryItemsTransfer = 68;
	mRenderName = 2;
	mAllowPhysicalPrims = TRUE;
	skyUseClassicClouds = TRUE;
	mEnableTeenMode = FALSE;
	mEnforceMaxBuild = TRUE;
	mRenderWater = TRUE;
	mVoiceConnector = "SLVoice";
	mMaxSelectDistance = 192.0f;
	mTerrainScale = 16.0f;
	mMinHoleSize = 0.05f;
	mMaxHollow = 0.95f;
	mMaxPrimScale = 64.0f;
	mDrawDistance = 128;
	mLockedDrawDistance = FALSE;
}

