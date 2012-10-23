/* Copyright (c) 2009
 *
 * Modular Systems All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *   3. Neither the name Modular Systems Ltd nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY MODULAR SYSTEMS LTD AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MODULAR SYSTEMS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "llagent.h"
#include "llfloater.h"
#include "llselectmgr.h"

#define FOLLOW_PERMS 1
#define PROP_REQUEST_KICK 10
#define INV_REQUEST_KICK 10

struct PropertiesRequest_t
{
	time_t	request_time;
	LLUUID	target_prim;
	U32		localID;
	U32		num_retries;
};

struct InventoryRequest_t
{
	time_t	request_time;
	LLViewerObject * object; // I can't be bothered to itterate the objects list on every kick, so hold the pointer here
	LLViewerObject * real_object; //for when we need to know what object the request is "actually" for (if using surrogates)
	U32		num_retries;
};


class ExportTrackerFloater : public LLFloater
{
public:
	void draw();
	//static ExportTrackerFloater* getInstance() { return sInstance; }
	virtual ~ExportTrackerFloater();
	//close me
	//static void close();
	void refresh();
	BOOL postBuild(void);

	void show();
	ExportTrackerFloater(const LLSD& seed);	
	//static ExportTrackerFloater* sInstance;

	//BOOL handleMouseDown(S32 x,S32 y,MASK mask);
	//BOOL handleMouseUp(S32 x,S32 y,MASK mask);
	//BOOL handleHover(S32 x,S32 y,MASK mask);

	//static void 	onCommitPosition(LLUICtrl* ctrl, void* userdata);

	//Import button
	void onClickExport();
	void addAvatarStuff(LLVOAvatar* avatarp);
	void buildExportListFromSelection();
	void buildExportListFromAvatar(LLVOAvatar* avatarp);

	//List handling
	void onClickSelectAll();
	void onClickSelectNone();
	void onClickSelectObjects();
	void onClickSelectWearables();
	void onChangeFilter(std::string filter);
	void buildListDisplays();
	void addObjectToList(LLUUID id, BOOL checked, std::string name, std::string type, LLUUID owner_id);
	
	void RemoteStart(LLDynamicArray<LLViewerObject*> catfayse, int primcount, std::string destination);

	//Close button
	void onClickClose();


	static LLDynamicArray<LLViewerObject*> mObjectSelection;
	static LLDynamicArray<LLViewerObject*> mObjectSelectionWaitList;

private:
	enum LIST_COLUMN_ORDER
	{
		LIST_CHECKED,
		LIST_TYPE,
		LIST_NAME,
		LIST_AVATARID
	};
	LLObjectSelectionHandle mSelection;

};

class JCExportTracker : public LLVOInventoryListener
{
private:
	static JCExportTracker* sInstance;
public:
	JCExportTracker();
	~JCExportTracker();
	static void close();
	static JCExportTracker* getInstance(){
		if(sInstance == NULL)
			sInstance = new JCExportTracker();
		return sInstance;
	}
	void init(ExportTrackerFloater* export_tracker_instance);
	void cleanup();
	void exportTexture(LLUUID assetid, std::string path, std::string name);
public:
	enum ExportState { IDLE, EXPORTING };
	static void processObjectProperties(LLMessageSystem* msg, void** user_data);
	void actuallyprocessObjectProperties(LLMessageSystem* msg);
	void inventoryChanged(LLViewerObject* obj,
								 LLInventoryObject::object_list_t* inv,
								 S32 serial_num,
								 void* data);
	static void			onFileLoadedForSave( 
							BOOL success,
							LLViewerFetchedTexture *src_vi,
							LLImageRaw* src, 
							LLImageRaw* aux_src,
							S32 discard_level, 
							BOOL final,
							void* userdata );

	LLSD* checkInventoryContents(InventoryRequest_t* original_request, LLInventoryObject::object_list_t* inv);

	bool serialize(LLDynamicArray<LLViewerObject*> objects);

	bool getAsyncData(LLViewerObject * obj);

	void requestInventory(LLViewerObject * obj, LLViewerObject * surrogate_obj=NULL);

	BOOL processSurrogate(LLViewerObject * surrogate_object);
	void createSurrogate(LLViewerObject * object);
	void removeSurrogates();
	void removeObject(LLViewerObject* obj);

	BOOL isSurrogateDeleteable(LLViewerObject* obj);
	
	LLUUID getUUIDForAsset(LLInventoryItem* item);

	BOOL exportAllowed(const LLPermissions *perm);
	LLUUID check_texture_perm(LLUUID id, bool isSculpt);

	void error(std::string name, U32 localid, LLVector3 object_pos, std::string error_msg);

	//Export idle callback
	static void exportworker(void *userdata);
	static void propertyworker(void *userdata);
	void doexportworker(void *userdata);
	void dopropertyworker(void *userdata);
	//end
	bool serializeSelection();
	void finalize();

	static BOOL mirror(LLInventoryObject* item, LLViewerObject* container = NULL, std::string root = "", std::string iname = "");
private:
	LLSD* subserialize(LLViewerObject* linkset);
	static void requestPrimProperties(U32 localID);
	void download(LLInventoryObject* item, LLViewerObject* container = NULL, std::string root = "", std::string iname = ""); //for adding to export floater and download using mirror

	U32 mStatus;
public:
	BOOL export_properties;
	BOOL export_inventory;
	BOOL export_tga;
	BOOL export_j2c;

	BOOL export_is_avatar;

	BOOL using_surrogates;

	//static U32 level;
	LLVector3 selection_center;
	LLVector3 selection_size;

	U32		propertyqueries;
	U32		invqueries;
	U32		mTotalPrims;
	U32		mLinksetsExported;
	U32		mPropertiesReceived;
	U32		mInventoriesReceived;
	U32		mPropertiesQueries;
	U32		mAssetsExported;
	U32		mTexturesExported;
	U32		mTotalObjects;
	U32		mTotalLinksets;
	U32		mTotalAssets;
	U32		mTotalTextures;

	ExportTrackerFloater* mExportFloater;

	std::set<LLUUID> mRequestedTextures;

	std::list<PropertiesRequest_t*> requested_properties;
	std::list<InventoryRequest_t*> requested_inventory;

	std::list<LLSD *> processed_prims;
	std::map<LLUUID,LLSD *>received_inventory;
	std::map<LLUUID,LLSD *>received_properties;

	
//magic positions and the object ids they belong to
	std::map<LLVector3, LLUUID> expected_surrogate_pos;
//list of surrogate object roots, mainly used for cleanup after the export is complete
	std::list<LLViewerObject *> surrogate_roots;
//magic positions and the object ids they belong to
	std::deque<LLViewerObject *> queued_surrogates;

	std::string destination;
private:
	static LLSD total;

	static std::string asset_dir;
public:
	//private variable accessors
	U32 getStatus(){return mStatus;}
};

// zip a folder. this doesn't work yet.
BOOL zip_folder(const std::string& srcfile, const std::string& dstfile);
