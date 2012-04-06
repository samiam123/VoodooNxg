/** 
 * @file llviewertexturelist.h
 * @brief Object for managing the list of images within a region
 *
 * $LicenseInfo:firstyear=2000&license=viewergpl$
 * 
 * Copyright (c) 2000-2010, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlife.com/developers/opensource/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlife.com/developers/opensource/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 * 
 */

#ifndef LL_LLVIEWERTEXTURELIST_H					
#define LL_LLVIEWERTEXTURELIST_H

#include "lluuid.h"
//#include "message.h"
#include "llgl.h"
#include "llstat.h"
#include "llviewertexture.h"
#include "llui.h"
#include <list>
#include <set>

const U32 LL_IMAGE_REZ_LOSSLESS_CUTOFF = 128;

const BOOL MIPMAP_YES = TRUE;
const BOOL MIPMAP_NO = FALSE;

const BOOL GL_TEXTURE_YES = TRUE;
const BOOL GL_TEXTURE_NO = FALSE;

const BOOL IMMEDIATE_YES = TRUE;
const BOOL IMMEDIATE_NO = FALSE;

class LLImageJ2C;
class LLMessageSystem;
class LLTextureView;

typedef	void (*LLImageCallback)(BOOL success,
								LLViewerFetchedTexture *src_vi,
								LLImageRaw* src,
								LLImageRaw* src_aux,
								S32 discard_level,
								BOOL final,
								void* userdata);

class LLViewerTextureList
{
    LOG_CLASS(LLViewerTextureList);

	friend class LLTextureView;
	friend class LLViewerTextureManager;
	
public:
	static BOOL createUploadFile(const std::string& filename, const std::string& out_filename, const U8 codec);
	static LLPointer<LLImageJ2C> convertToUploadFile(LLPointer<LLImageRaw> raw_image);
	static void processImageNotInDatabase( LLMessageSystem *msg, void **user_data );
	static S32 calcMaxTextureRAM();
	static void receiveImageHeader(LLMessageSystem *msg, void **user_data);
	static void receiveImagePacket(LLMessageSystem *msg, void **user_data);

public:
	LLViewerTextureList();
	~LLViewerTextureList();

	void init();
	void shutdown();
	void dump();
	void destroyGL(BOOL save_state = TRUE);
	void restoreGL();
	BOOL isInitialized() const {return mInitialized;}

	LLViewerFetchedTexture *findImage(const LLUUID &image_id);

	void dirtyImage(LLViewerFetchedTexture *image);
	
	// Using image stats, determine what images are necessary, and perform image updates.
	void updateImages(F32 max_time);
	void forceImmediateUpdate(LLViewerFetchedTexture* imagep) ;

	// Decode and create textures for all images currently in list.
	void decodeAllImages(F32 max_decode_time); 

	void handleIRCallback(void **data, const S32 number);

	void setUpdateStats(BOOL b)			{ mUpdateStats = b; }

	S32	getMaxResidentTexMem() const	{ return mMaxResidentTexMemInMegaBytes; }
	S32 getMaxTotalTextureMem() const   { return mMaxTotalTextureMemInMegaBytes;}
	S32 getNumImages()					{ return mImageList.size(); }

	void updateMaxResidentTexMem(S32 mem);
	
	void doPreloadImages();
	void doPrefetchImages();

	static S32 getMinVideoRamSetting();
	static S32 getMaxVideoRamSetting(bool get_recommended = false);
	
private:
	void updateImagesDecodePriorities();
	F32  updateImagesCreateTextures(F32 max_time);
	F32  updateImagesFetchTextures(F32 max_time);
	void updateImagesUpdateStats();

public:
	void addImage(LLViewerFetchedTexture *image);
	void deleteImage(LLViewerFetchedTexture *image);
/*
private: // commented out for region console and moved down sams voodoo

	void addImageToList(LLViewerFetchedTexture *image);
	void removeImageFromList(LLViewerFetchedTexture *image);
*/
	LLViewerFetchedTexture * getImage(const LLUUID &image_id,									 
									 BOOL usemipmap = TRUE,
									 LLViewerTexture::EBoostLevel boost_priority = LLViewerTexture::BOOST_NONE,		// Get the requested level immediately upon creation.
									 S8 texture_type = LLViewerTexture::FETCHED_TEXTURE,
									 LLGLint internal_format = 0,
									 LLGLenum primary_format = 0,
									 LLHost request_from_host = LLHost()
									 );
private: 
  void addImageToList(LLViewerFetchedTexture *image); 
  void removeImageFromList(LLViewerFetchedTexture *image); 
	
	LLViewerFetchedTexture * getImageFromFile(const std::string& filename,									 
									 BOOL usemipmap = TRUE,
									 LLViewerTexture::EBoostLevel boost_priority = LLViewerTexture::BOOST_NONE,		// Get the requested level immediately upon creation.
									 S8 texture_type = LLViewerTexture::FETCHED_TEXTURE,
									 LLGLint internal_format = 0,
									 LLGLenum primary_format = 0,
									 const LLUUID& force_id = LLUUID::null
									 );
	
	LLViewerFetchedTexture* getImageFromUrl(const std::string& url,
									 BOOL usemipmap = TRUE,
									 LLViewerTexture::EBoostLevel boost_priority = LLViewerTexture::BOOST_NONE,		// Get the requested level immediately upon creation.
									 S8 texture_type = LLViewerTexture::FETCHED_TEXTURE,
									 LLGLint internal_format = 0,
									 LLGLenum primary_format = 0,
									 const LLUUID& force_id = LLUUID::null
									 );

	LLViewerFetchedTexture* createImage(const LLUUID &image_id,
									 BOOL usemipmap = TRUE,
									 LLViewerTexture::EBoostLevel boost_priority = LLViewerTexture::BOOST_NONE,		// Get the requested level immediately upon creation.
									 S8 texture_type = LLViewerTexture::FETCHED_TEXTURE,
									 LLGLint internal_format = 0,
									 LLGLenum primary_format = 0,
									 LLHost request_from_host = LLHost()
									 );
	
	// Request image from a specific host, used for baked avatar textures.
	// Implemented in header in case someone changes default params above. JC
	LLViewerFetchedTexture* getImageFromHost(const LLUUID& image_id, LLHost host)
	{ return getImage(image_id, TRUE, LLViewerTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE, 0, 0, host); }

public:
	typedef std::set<LLPointer<LLViewerFetchedTexture> > image_list_t;	
	image_list_t mLoadingStreamList;
	image_list_t mCreateTextureList;
	image_list_t mCallbackList;

	// Note: just raw pointers because they are never referenced, just compared against
	std::set<LLViewerFetchedTexture*> mDirtyTextureList;
	
	BOOL mForceResetTextureStats;
    
private:
	typedef std::map< LLUUID, LLPointer<LLViewerFetchedTexture> > uuid_map_t;
	uuid_map_t mUUIDMap;
	LLUUID mLastUpdateUUID;
	LLUUID mLastFetchUUID;
	
	typedef std::set<LLPointer<LLViewerFetchedTexture>, LLViewerFetchedTexture::Compare> image_priority_list_t;	
	image_priority_list_t mImageList;

	// simply holds on to LLViewerFetchedTexture references to stop them from being purged too soon
	std::set<LLPointer<LLViewerFetchedTexture> > mImagePreloads;

	BOOL mInitialized ;
	BOOL mUpdateStats;
	S32	mMaxResidentTexMemInMegaBytes;
	S32 mMaxTotalTextureMemInMegaBytes;
	LLFrameTimer mForceDecodeTimer;
	
public:
	static U32 sTextureBits;
	static U32 sTexturePackets;

private:
	static S32 sNumImages;
	static void (*sUUIDCallback)(void**, const LLUUID &);
};

class LLUIImageList : public LLImageProviderInterface, public LLSingleton<LLUIImageList>
{
public:
	// LLImageProviderInterface
	/*virtual*/ LLPointer<LLUIImage> getUIImageByID(const LLUUID& id, S32 priority);
	/*virtual*/ LLPointer<LLUIImage> getUIImage(const std::string& name, S32 priority);
	void cleanUp();

	bool initFromFile();

	LLPointer<LLUIImage> preloadUIImage(const std::string& name, const std::string& filename, BOOL use_mips, const LLRect& scale_rect, const LLRect& clip_rect);
	
	static void onUIImageLoaded( BOOL success, LLViewerFetchedTexture *src_vi, LLImageRaw* src, LLImageRaw* src_aux, S32 discard_level, BOOL final, void* userdata );
private:
	LLPointer<LLUIImage> loadUIImageByName(const std::string& name, const std::string& filename,
		                           BOOL use_mips = FALSE, const LLRect& scale_rect = LLRect::null, 
								   const LLRect& clip_rect = LLRect::null,
		                           LLViewerTexture::EBoostLevel boost_priority = LLViewerTexture::BOOST_UI);
	LLPointer<LLUIImage> loadUIImageByID(const LLUUID& id,
								 BOOL use_mips = FALSE, const LLRect& scale_rect = LLRect::null, 
								 const LLRect& clip_rect = LLRect::null,
								 LLViewerTexture::EBoostLevel boost_priority = LLViewerTexture::BOOST_UI);

	LLPointer<LLUIImage> loadUIImage(LLViewerFetchedTexture* imagep, const std::string& name, BOOL use_mips = FALSE, const LLRect& scale_rect = LLRect::null, const LLRect& clip_rect = LLRect::null);


	struct LLUIImageLoadData
	{
		std::string mImageName;
		LLRect mImageScaleRegion;
		LLRect mImageClipRegion;
	};

	typedef std::map< std::string, LLPointer<LLUIImage> > uuid_ui_image_map_t;
	uuid_ui_image_map_t mUIImages;

	//
	//keep a copy of UI textures to prevent them to be deleted.
	//mGLTexturep of each UI texture equals to some LLUIImage.mImage.
	std::list< LLPointer<LLViewerFetchedTexture> > mUITextureList ;
};

const BOOL GLTEXTURE_TRUE = TRUE;
const BOOL GLTEXTURE_FALSE = FALSE;
const BOOL MIPMAP_TRUE = TRUE;
const BOOL MIPMAP_FALSE = FALSE;

extern LLViewerTextureList gTextureList;

#endif
