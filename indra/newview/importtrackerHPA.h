/** 
 * @file importtracker.h
 * @brief A utility for importing linksets from XML.
 * Discrete wuz here
 */

#ifndef IMPORTTRACKER_H
#define IMPORTTRACKER_H

#define MAX_IDLE_TIME 20

#include "llviewerobject.h"
#include "llfloater.h"

class LLSpinCtrl;

struct InventoryImportInfo
{
	U32 localid;
	LLAssetType::EType type;
	LLInventoryType::EType inv_type;
	LLWearableType::EType wear_type;
	LLTransactionID tid;
	LLUUID assetid;
	std::string name;
	std::string description;
	bool compiled;
	std::string filename;
	U32 perms;
	U32 retries;
};

class ImportTrackerFloater : public LLFloater
{
public:
	void draw();
	//static ImportTrackerFloater* getInstance();
	virtual ~ImportTrackerFloater();
	//close me
	//static void close();
	BOOL postBuild(void);
	void showOpenfile();
	ImportTrackerFloater(const LLSD& seed);	
	//static ImportTrackerFloater* sInstance;

	BOOL handleMouseDown(S32 x,S32 y,MASK mask);
	BOOL handleMouseUp(S32 x,S32 y,MASK mask);
	BOOL handleHover(S32 x,S32 y,MASK mask);

	static void 	onCommitPosition(LLUICtrl* ctrl, void* userdata);

	//Temporary function to rez a cube.
	void onClickPlywood();

	//Reset button
	void onClickReset();

	//My Position button
	void onClickSetToMyPosition();

	//Import button
	void onClickImport();
	
	//Close button
	void onClickClose();

	LLSpinCtrl*		mCtrlPosX;
	LLSpinCtrl*		mCtrlPosY;
	LLSpinCtrl*		mCtrlPosZ;

	static int		mTotalObjects;
	static int		mObjectsImported;
	static int		mTotalLinksets;
	static int		mTotalTextures;
	static int		mLinksetsImported;
	static int		mTexturesImported;
	static int		mTotalAssets;
	static int		mAssetsImported;
	static int		mAssetsUploaded;

protected:
	void			sendPosition();
};

class ImportTracker
{
	public:
		enum ImportState { IDLE, WAND, BUILDING, LINKING, POSITIONING };			
		
		ImportTracker()
		: numberExpected(0),
		state(IDLE),
		last(0),
		groupcounter(0),
		updated(0)
		{ }
		ImportTracker(LLSD &data) { state = IDLE; linkset = data; numberExpected=0;}
		~ImportTracker() { localids.clear(); linkset.clear(); }
	
		//Chalice - support import of linkset groups
		LLSD parse_hpa_group(LLXmlTreeNode* prim);
		LLSD parse_hpa_linkset(LLXmlTreeNode* prim);
		LLSD parse_hpa_object(LLXmlTreeNode* prim);
		void loadhpa(std::string file);
		void importer(std::string file, void (*callback)(LLViewerObject*));
		static void plywoodtracker(void *userdata);
		void cleargroups();
		void import(LLSD &ls_data);
		void expectRez();
		void clear();
		void finish();
		void cleanUp();
		void get_update(S32 newid, BOOL justCreated = false, BOOL createSelected = false);
		
		const int getState() { return state; }

		U32 mTotalLinksets;
		U32 objects;
		U32 textures;
		LLSD linksets;
		U32 asset_insertions;
		LLVector3 importposition;
		LLVector3 size;
		LLVector3 importoffset;
		LLVector3 currentimportoffset;

		time_t	idle_time;

		//Working LLSD holders
		LLUUID current_asset;
		
		// Rebase map
		std::map<LLUUID,LLUUID> assetmap;
		
		//Export texture list
		std::list<LLUUID> uploadtextures;

		//Update map from texture worker
		void update_map(LLUUID uploaded_asset);

		//Move to next texture upload
		void upload_next_asset();

		static void import_asset(InventoryImportInfo* data);

	protected:		
		void send_inventory(LLSD &prim);
		void send_properties(LLSD &prim, int counter);
		void send_vectors(LLSD &prim, int counter);
		void send_shape(LLSD &prim);
		void send_image(LLSD &prim);
		void send_extras(LLSD &prim);
		void send_namedesc(LLSD &prim);
		void link();
		void wear(LLSD &prim);
		void position(LLSD &prim);
	public:
		void plywood_above_head();

		int					state;
		LLSD				linksetgroups;
		LLSD				linkset;
		int					groupcounter;
	
	private:
		int				numberExpected;
		S32				last;
		LLVector3			root;
		LLQuaternion		rootrot;
		std::list<S32>			localids;
		int					updated;
		LLVector3			linksetoffset;
		LLVector3			initialPos;

		std::string filepath;
		std::string asset_dir;
		void	(*mDownCallback)(LLViewerObject*);

		U32 lastrootid;
};

extern ImportTracker gImportTracker;

//extern LLAgent gAgent;

#endif
