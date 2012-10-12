/** 
 * @file hbprefsinert.h
 * @brief  Ascent Viewer preferences panel
 *
 * $LicenseInfo:firstyear=2008&license=viewergpl$
 * 
 * Copyright (c) 2008, Henri Beauchamp.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#ifndef ASCENTPREFSSYS_H
#define ASCENTPREFSSYS_H


#include "llpanel.h"
#include "lldroptarget.h"


class LLPrefsAscentSys : public LLPanel
{
public:
    LLPrefsAscentSys();
    ~LLPrefsAscentSys();

    void apply();
    void cancel();
    void refresh();
    void refreshValues();

protected:
    static void onCommitCheckBox(LLUICtrl* ctrl, void* user_data);
    static void onCommitCmdLine(LLUICtrl* ctrl, void* user_data);
    static void onCommitComboBox(LLUICtrl* ctrl, void* user_data);
    static void onCommitTexturePicker(LLUICtrl* ctrl, void* user_data);

    //General -----------------------------------------------------------------------------
    BOOL mDoubleClickTeleport;
        BOOL mResetCameraAfterTP;
        BOOL mOffsetTPByUserHeight;
    BOOL mPreviewAnimInWorld;
//    BOOL mSaveScriptsAsMono;
    BOOL mAlwaysRezInGroup;
    BOOL mBuildAlwaysEnabled;
    BOOL mAlwaysShowFly;
    BOOL mDisableMinZoom;
    BOOL mPowerUser;
    BOOL mUseSystemFolder;
        BOOL mUploadToSystem;
    BOOL mFetchInventoryOnLogin;
    BOOL mEnableLLWind;
    BOOL mEnableClouds;
        BOOL mEnableClassicClouds;
    BOOL mSpeedRez;
        U32 mSpeedRezInterval;

    //Command Line ------------------------------------------------------------------------
    BOOL mCmdLine;
    std::string mCmdLinePos;
    std::string mCmdLineGround;
    std::string mCmdLineHeight;
    std::string mCmdLineTeleportHome;
    std::string mCmdLineRezPlatform;
    F32 mCmdPlatformSize;
    std::string mCmdLineCalc;
    std::string mCmdLineClearChat;
    std::string mCmdLineDrawDistance;
    std::string mCmdTeleportToCam;
    std::string mCmdLineKeyToName;
    std::string mCmdLineOfferTp;
    std::string mCmdLineMapTo;
    BOOL mCmdMapToKeepPos;
    std::string mCmdLineTP2;
    std::string mCmdLineAway;

    //Security ----------------------------------------------------------------------------
    BOOL mBroadcastViewerEffects;
    BOOL mDisablePointAtAndBeam;
    BOOL mPrivateLookAt;
    BOOL mShowLookAt;
	BOOL mQuietSnapshotsToDisk;
	BOOL mDetachBridge;
	BOOL mRevokePermsOnStandUp;
    BOOL mDisableClickSit;
    BOOL mDisplayScriptJumps;
    F32 mNumScriptDiff;
	//Build -------------------------------------------------------------------------------
	F32 mAlpha;
	LLColor4 mColor;
	BOOL mFullBright;
	F32 mGlow;
	std::string mMaterial;
	BOOL mNextCopy;
	BOOL mNextMod;
	BOOL mNextTrans;
	std::string mShiny;
	BOOL mTemporary;
	std::string mTexture;
	BOOL mPhantom;
	BOOL mPhysical;
	F32 mXsize;
	F32 mYsize;
	F32 mZsize;
private:
	static LLPrefsAscentSys* sInst;
	static void SinguBuildItemDrop(LLViewerInventoryItem* item);
};

#endif
