#ifndef __HIPPO_LIMITS_H__
#define __HIPPO_LIMITS_H__


class HippoLimits
{
	LOG_CLASS(HippoLimits);
public:
	HippoLimits();

	int   getMaxAgentGroups() const { return mMaxAgentGroups; }
	float getMaxHeight()      const { return mMaxHeight;      }
	float getMinHoleSize()    const { return mMinHoleSize;    }
	float getMaxHollow()      const { return mMaxHollow;      }
	float getMaxPrimScale()   const { return mMaxPrimScale;   }
	float getMaxLinkedPrims() const { return mMaxLinkedPrims; }
	float getMaxPhysLinkedPrims() const { return mMaxPhysLinkedPrims; }
	float getMaxInventoryItemsTransfer() const { return mMaxInventoryItemsTransfer; }
	const std::string& getVoiceConnector()  const { return mVoiceConnector; }
	float getMaxSelectDistance() const { return mMaxSelectDistance; }
	// Returns the max prim size we can use on a grid
	F32	getMinPrimScale() const;
	//F32	getMaxPrimScale() const;

	F32 getMaxDragDistance() const;

	F32	getMinPrimXPos() const;
	F32	getMinPrimYPos() const;
	F32	getMinPrimZPos() const;
	F32	getMaxPrimXPos() const;
	F32	getMaxPrimYPos() const;
	F32	getMaxPrimZPos() const;
	void setLimits();

//private:
	int     mMaxAgentGroups;
	float   mMaxHeight;
	float   mMinHoleSize;
	float   mMaxHollow;
	float   mMaxPrimScale;
	F32		mMinPrimScale;
	S32		mMaxLinkedPrims;
	S32		mMaxPhysLinkedPrims;
	F32		mMaxPrimXPos;
	F32		mMaxPrimYPos;
	F32		mMaxPrimZPos;
	F32		mMinPrimXPos;
	F32		mMinPrimYPos;
	F32		mMinPrimZPos;
	F32		mMaxSelectDistance;
	F32     mTerrainScale;
	S32     mRenderName;

	F32		mMaxInventoryItemsTransfer;
	F32     mMaxDragDistance;

	BOOL    skyUseClassicClouds;
	BOOL    mAllowParcelWindLight;
	BOOL    mAllowMinimap;
	BOOL    mAllowPhysicalPrims;
	BOOL    mEnableTeenMode;
	BOOL    mEnforceMaxBuild;
	BOOL    mRenderWater;
	std::string    mVoiceConnector;
private:
	void    setOpenSimLimits();
	void    setAuroraLimits();
	void    setSecondLifeLimits();
	void	setSecondLifeMaxPrimScale();
};


extern HippoLimits *gHippoLimits;


#endif
