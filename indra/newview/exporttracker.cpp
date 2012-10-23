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

//jcool410 was here ;P
//HPA export and more added -Patrick Sapinski (Wednesday, November 25, 2009)
//Object selection (checkboxes), avatar handling, minor updates to the UI - Duncan Garrett (Oct 04, 2010)

#include "llviewerprecompiledheaders.h"

#include "exporttracker.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"
#include "llsdutil.h"
#include "llsdserialize.h"
#include "lldirpicker.h"
#include "llfilepicker.h"
#include "llviewerregion.h"
#include "llwindow.h"
#include "lltransfersourceasset.h"
#include "lltooldraganddrop.h"
#include "llviewernetwork.h"
#include "llcurl.h"
#include "llscrolllistctrl.h"
#include "llselectmgr.h"
#include "llappviewer.h"
#include "llviewercontrol.h"
#include "llinventorymodel.h"
#include "llappviewer.h"
#include "lltexturecache.h"
#include "llinventoryfunctions.h"
#include "llprogressbar.h"

#include "llimagej2c.h"
#include "llimagetga.h"

#include "llviewertexteditor.h"
#include "lllogchat.h" //for timestamp
#include "lluictrlfactory.h"
#include "llcheckboxctrl.h"
#include "llcallbacklist.h"

#include "llvotreenew.h"
#include "lltrans.h"
#include "llsdutil_math.h"
#include "llviewertexturelist.h"
#include "llversioninfo.h"
#include "llvfs.h"
#include "llfloaterreg.h"
#include "llviewertexteditor.h"

#include "llvoavatardefines.h"
using namespace LLVOAvatarDefines;

#include <fstream>
using namespace std;

#ifdef LL_STANDALONE
# include <zlib.h>
#else
# include "zlib/zlib.h"
#endif
void cmdline_printchat(std::string message);
JCExportTracker* JCExportTracker::sInstance;
LLSD JCExportTracker::total;
std::string JCExportTracker::asset_dir;

LLDynamicArray<LLViewerObject*> ExportTrackerFloater::mObjectSelection;
LLDynamicArray<LLViewerObject*> ExportTrackerFloater::mObjectSelectionWaitList;

//Export Floater constructor
ExportTrackerFloater::ExportTrackerFloater(const LLSD& seed) :
	LLFloater( seed )
{
	llinfos << "Loading Floater..." << llendl;
	//LLUICtrlFactory::getInstance()->buildFloater( this, "floater_prim_export.xml" );
}

BOOL ExportTrackerFloater::postBuild()
{
	childSetAction("export", boost::bind(&ExportTrackerFloater::onClickExport, this));
	childSetAction("close", boost::bind(&ExportTrackerFloater::onClickClose, this));
	childSetAction("select_all_btn", boost::bind(&ExportTrackerFloater::onClickSelectAll, this));
	childSetAction("select_none_btn", boost::bind(&ExportTrackerFloater::onClickSelectNone, this));
	childSetAction("select_objects_btn", boost::bind(&ExportTrackerFloater::onClickSelectObjects, this));
	childSetAction("select_wearables_btn", boost::bind(&ExportTrackerFloater::onClickSelectWearables, this));
	childSetEnabled("export",true);

	childSetEnabled("export_tga",true);
	childSetEnabled("export_j2c",true);
	childSetEnabled("export_properties",true);
	childSetEnabled("export_contents",true);

	mObjectSelection.clear();
	mSelection = LLSelectMgr::getInstance()->getSelection();
	llinfos << "Entered postbuild, found " << mSelection->getNumNodes() << " nodes." << llendl;
	if(!mSelection) 
	{
		llwarns << "No selection, cancelling." << llendl;
		return true;
	}
	if(mSelection->getRootObjectCount() < 1) 
	{
		llwarns << "Selection has no root object, cancelling." << llendl;
		return true;
	}
	
	//from serializeselection
	JCExportTracker::getInstance()->init(this);
	
	gIdleCallbacks.deleteFunction(JCExportTracker::exportworker);
	
	//Only attachments have sub-parents (?)
	if (mSelection->getFirstRootObject()->getSubParent())
	{
		JCExportTracker::getInstance()->export_is_avatar = true;
		buildExportListFromAvatar((LLVOAvatar*)mSelection->getFirstRootObject()->getSubParent());
	}
	else
	{
		JCExportTracker::getInstance()->export_is_avatar = false;
		buildExportListFromSelection();
	}
	
	gIdleCallbacks.addFunction(JCExportTracker::propertyworker, NULL);
	
	addObjectToList(
		LLUUID::null,
		false,
		"Loading...",
		"Object_Multi",
		LLUUID::null);

	refresh();
	return true;
}

void ExportTrackerFloater::buildListDisplays()
{
	//update export queue list
	LLScrollListCtrl* mResultList;
	mResultList = getChild<LLScrollListCtrl>("object_result_list");

	mResultList->deleteAllItems();

	LLDynamicArray<LLViewerObject*> objectlist;
	std::map<LLViewerObject*, bool> avatars;

	if (mObjectSelectionWaitList.empty())
	{
		objectlist = mObjectSelection;
	}
	else 
	{
		objectlist = mObjectSelectionWaitList;
	}

	LLDynamicArray<LLViewerObject*>::iterator iter=objectlist.begin();
	LLViewerObject* object = NULL;

	llinfos << "Beginning object listing." << llendl;

	for(;iter!=objectlist.end();iter++)
	{
		object = (*iter);
		std::string objectp_id = llformat("%d", object->getLocalID());

		if(mResultList->getItemIndex(object->getID()) == -1)
		{
			bool is_attachment = false;
			bool is_root = true;
			LLViewerObject* parentp = object->getSubParent();
			if(parentp)
			{
				if(parentp->isAvatar())
				{
					// parent is an avatar
					is_attachment = true;
				}
			}

			bool is_prim = true;
			if(object->getPCode() >= LL_PCODE_APP)
			{
				is_prim = false;
			}

			if(is_root && is_prim)
			{
				std::string name = "";
				LLSD * props = JCExportTracker::getInstance()->received_properties[object->getID()];
				if(props != NULL)
				{
					name = std::string((*props)["name"]);
				}
				if (is_attachment)
				{
					std::string joint = LLTrans::getString(gAgentAvatarp->getAttachedPointName(object->getAttachmentItemID()));
					name += " (worn on " + utf8str_tolower(joint) + ")";
				}

				addObjectToList(
					object->getID(),
					true,
					name,
					"Object",
					LLUUID::null);
			}
			/*else if(is_avatar)
			{
				if(!avatars[object])
				{
					llinfos << "Avatar - adding later." << llendl;
					avatars[object] = true;
				}
			}*/
		}
	}
	/*std::map<LLViewerObject*, bool>::iterator avatar_iter = avatars.begin();
	std::map<LLViewerObject*, bool>::iterator avatars_end = avatars.end();
	for( ; avatar_iter != avatars_end; avatar_iter++)
	{
		LLViewerObject* avatar = (*avatar_iter).first;
		addAvatarStuff((LLVOAvatar*)avatar);
	}*/


	std::list<LLSD *>::iterator iter2=JCExportTracker::getInstance()->processed_prims.begin();
	for(;iter2!=JCExportTracker::getInstance()->processed_prims.end();iter2++)
	{	// for each object

		LLSD *plsd=(*iter2);
		//llwarns<<LLSD::dump(*plsd)<<llendl;
		for(LLSD::array_iterator link_itr = plsd->beginArray();
			link_itr != plsd->endArray();
			++link_itr)
		{ 
			LLSD prim = (*link_itr);

			LLSD element;
			element["id"] = prim["id"]; //object->getLocalID();

			element["columns"][0]["column"] = "Name";
			element["columns"][0]["type"] = "text";
			element["columns"][0]["value"] = "";//name;

			element["columns"][1]["column"] = "UUID";
			element["columns"][1]["type"] = "text";
			element["columns"][1]["value"] = "";

			element["columns"][2]["column"] = "icon_prop";
			element["columns"][2]["type"] = "icon";
			element["columns"][2]["value"] = "lag_status_good.tga";

			element["columns"][3]["column"] = "icon_inv";
			element["columns"][3]["type"] = "icon";
			element["columns"][3]["value"] = "lag_status_good.tga";

			element["columns"][4]["column"] = "Local ID";
			element["columns"][4]["type"] = "text";
			element["columns"][4]["value"] = prim["id"];

			LLVector3 position;
			position.setVec(ll_vector3d_from_sd(prim["position"]));

			std::stringstream sstr;	
			sstr <<llformat("%.1f", position.mV[VX]);
			sstr <<","<<llformat("%.1f", position.mV[VY]);
			sstr <<","<<llformat("%.1f", position.mV[VZ]);

			element["columns"][5]["column"] = "Position";
			element["columns"][5]["type"] = "text";
			element["columns"][5]["value"] = sstr.str();

			std::list<PropertiesRequest_t *>::iterator iter=JCExportTracker::getInstance()->requested_properties.begin();
			for(;iter!=JCExportTracker::getInstance()->requested_properties.end();iter++)
			{
				PropertiesRequest_t * req=(*iter);
				if(req->localID==object->getLocalID())
				{
					cmdline_printchat("match");
					element["columns"][6]["column"] = "Retries";
					element["columns"][6]["type"] = "text";
					element["columns"][6]["value"] = llformat("%u",req->num_retries);;
				}
			}

			mResultList->addElement(element, ADD_BOTTOM);
		}
	}

	refresh();
}

void ExportTrackerFloater::buildExportListFromSelection()
{
	LLDynamicArray<LLViewerObject*> objectArray;
	for (LLObjectSelection::valid_root_iterator iter = mSelection->valid_root_begin();
		iter != mSelection->valid_root_end(); iter++)
	{
		LLSelectNode* selectNode = *iter;
		LLViewerObject* object = selectNode->getObject();
		if(object)
		{
			if(FOLLOW_PERMS)
			{
				bool isCreator = true;
				if(LLGridManager::getInstance()->isInSLMain() || LLGridManager::getInstance()->isInSLBeta()) {
					isCreator = (selectNode->mPermissions->getCreator() == gAgent.getID());
				}
				if(!(object->permModify() && object->permCopy() && object->permTransfer()))
				{
					JCExportTracker::getInstance()->error("", object->getLocalID(), object->getPosition(), "Invalid Permissions");
					break;
				}
			}
			JCExportTracker::getInstance()->getAsyncData(object);
			mObjectSelection.put(object);
			JCExportTracker::getInstance()->mTotalLinksets += LLSelectMgr::getInstance()->getSelection()->getRootObjectCount();
			JCExportTracker::getInstance()->mTotalObjects += LLSelectMgr::getInstance()->getSelection()->getObjectCount();
		}
		else 
		{
			JCExportTracker::getInstance()->error("", object->getLocalID(), object->getPosition(), "Invalid Permissions");
		}
	}

	if (!JCExportTracker::getInstance()->export_is_avatar)
	{
		LLBBox bbox = LLSelectMgr::getInstance()->getBBoxOfSelection();
		LLVector3 box_center_agent = bbox.getCenterAgent();

		LLVector3 temp = bbox.getExtentLocal();

		std::stringstream sstr;	
		LLUICtrl* ctrl = this->getChild<LLUICtrl>("selection size");	

		sstr <<"X: "<<llformat("%.2f", temp.mV[VX]);
		sstr <<", Y: "<<llformat("%.2f", temp.mV[VY]);
		sstr <<", Z: "<<llformat("%.2f", temp.mV[VZ]);

		ctrl->setValue(LLSD("Text")=sstr.str());

		JCExportTracker::getInstance()->selection_size = bbox.getExtentLocal();
		JCExportTracker::getInstance()->selection_center = bbox.getCenterAgent();
	}
}

void ExportTrackerFloater::buildExportListFromAvatar(LLVOAvatar* avatarp)
{
	LLDynamicArray<LLViewerObject*> objectArray;
	for( S32 type = LLWearableType::WT_SHAPE; type < LLWearableType::WT_COUNT; type++ )
	{
		// guess whether this wearable actually exists
		// by checking whether it has any textures that aren't default
		bool exists = false;
		if(type == LLWearableType::WT_SHAPE)
		{
			exists = true;
		}
		else if (type == LLWearableType::WT_ALPHA || type == LLWearableType::WT_TATTOO) //alpha layers and tattos are unsupported for now
		{
			continue;
		}
		else
		{
			for( S32 te = 0; te < LLVOAvatarDefines::TEX_NUM_INDICES; te++ )
			{
				if( (S32)LLVOAvatarDictionary::getTEWearableType((LLVOAvatarDefines::ETextureIndex)te ) == type )
				{
					LLViewerTexture* te_image = avatarp->getTEImage( te );
					if( te_image->getID() != IMG_DEFAULT_AVATAR)
					{
						JCExportTracker::getInstance()->mTotalTextures++;
						//mRequestedTextures.insert(te_image->getID());
						exists = true;
						break;
					}
				}
			}
		}

		if(exists)
		{
			std::string wearable_name = LLWearableType::getTypeName((LLWearableType::EType)type );
			std::string name = avatarp->getFullname() + " " + wearable_name; //LK todoo, fix name

			LLUUID myid;
			myid.generate();

			addObjectToList(
				myid,
				false,
				name,
				wearable_name,
				avatarp->getID());
		}
	}

	// Add attachments
	LLSelectMgr::getInstance()->deselectAll();
	LLViewerObject::child_list_t child_list = avatarp->getChildren();
	for (LLViewerObject::child_list_t::iterator i = child_list.begin(); i != child_list.end(); ++i)
	{
		LLViewerObject* childp = *i;
		if(childp)
		{
			//Perform permissions check when running the selection!
			LLSelectMgr::getInstance()->selectObjectAndFamily(childp);
			buildExportListFromSelection();
			LLSelectMgr::getInstance()->deselectAll();
		}
	}

	

	LLBBox bbox = avatarp->getBoundingBoxAgent();  LLSelectMgr::getInstance()->getBBoxOfSelection();
	LLVector3 box_center_agent = avatarp->getPosition();

	LLVector3 temp = bbox.getExtentLocal();

	std::stringstream sstr;	
	LLUICtrl* ctrl = this->getChild<LLUICtrl>("selection size");	

	sstr <<"X: "<<llformat("%.2f", temp.mV[VX]);
	sstr <<", Y: "<<llformat("%.2f", temp.mV[VY]);
	sstr <<", Z: "<<llformat("%.2f", temp.mV[VZ]);

	ctrl->setValue(LLSD("Text")=sstr.str());

	JCExportTracker::getInstance()->selection_size = bbox.getExtentLocal();
	JCExportTracker::getInstance()->selection_center = bbox.getCenterAgent();
}

void ExportTrackerFloater::refresh()
{

	std::string status_text;

	if (JCExportTracker::getInstance()->getStatus() == JCExportTracker::IDLE)
		status_text = "idle";
	else
		status_text = "exporting";

	//is this a bad place for this function? -Patrick Sapinski (Friday, November 13, 2009)
	getChild<LLTextBox>("status label")->setValue(
		"Status: " + status_text
		+  llformat("\nObjects: %u/%u",				JCExportTracker::getInstance()->mLinksetsExported,	JCExportTracker::getInstance()->mTotalLinksets)
		+  llformat("\nPrimitives: %u/%u",			JCExportTracker::getInstance()->mTotalPrims,		JCExportTracker::getInstance()->mTotalObjects)
		+  llformat("\nProperties Received: %u/%u",	JCExportTracker::getInstance()->mPropertiesReceived, JCExportTracker::getInstance()->mTotalObjects)
		+  llformat("\nInventories Received: %u/%u",JCExportTracker::getInstance()->mInventoriesReceived, JCExportTracker::getInstance()->mTotalObjects)
		//+  llformat("\n   Pending Queries: %u",	JCExportTracker::getInstance()->requested_properties.size())
		+  llformat("\nInventory Items: %u/%u",		JCExportTracker::getInstance()->mAssetsExported,	JCExportTracker::getInstance()->mTotalAssets)
		//+  llformat("\n   Pending Queries: %u",	JCExportTracker::getInstance()->requested_inventory.size())
		+  llformat("\nTextures: %u/%u",			JCExportTracker::getInstance()->mTexturesExported,	JCExportTracker::getInstance()->mTotalTextures)
		);

	// Draw the progress bar.

	LLProgressBar *progress_bar = getChild<LLProgressBar>("progress_bar");
	F32 data_progress = 100.0f * ((F32)JCExportTracker::getInstance()->mTotalPrims / JCExportTracker::getInstance()->mTotalObjects);
	progress_bar->setValue(llfloor(data_progress));

	/*
	LLRect rec  = getChild<LLPanel>("progress_bar")->getRect();

	S32 bar_width = rec.getWidth();
	S32 left = rec.mLeft, right = rec.mRight;
	S32 top = rec.mTop;
	S32 bottom = rec.mBottom;

	gGL.color4f(0.f, 0.f, 0.f, 0.75f);
	gl_rect_2d(rec);

	F32 data_progress = ((F32)JCExportTracker::getInstance()->mTotalPrims / JCExportTracker::getInstance()->mTotalObjects);

	if (data_progress > 0.0f)
	{
		// Downloaded bytes
		right = left + llfloor(data_progress * (F32)bar_width);
		if (right > left)
		{
			gGL.color4f(0.f, 0.f, 1.f, 0.75f);
			gl_rect_2d(left, top, right, bottom);
		}
	}
	*/
}

void ExportTrackerFloater::onChangeFilter(std::string filter)
{
	LLScrollListCtrl* list = getChild<LLScrollListCtrl>("object_result_list");
	std::vector<LLScrollListItem*> items = list->getAllData();
	std::vector<LLScrollListItem*>::iterator item_iter = items.begin();
	std::vector<LLScrollListItem*>::iterator items_end = items.end();
	for( ; item_iter != items_end; ++item_iter)
	{
		LLScrollListItem* item = (*item_iter);
		std::string type = ((*item_iter)->getColumn(LIST_TYPE)->getValue()).asString();
		if ((type == filter)||(filter == "all")||(filter == "!object" && type != "icons/Inv_Object.png"))
			item->getColumn(LIST_CHECKED)->setValue(true);
		else
			item->getColumn(LIST_CHECKED)->setValue(false);
	}
}

void ExportTrackerFloater::onClickSelectAll()
{
	onChangeFilter("all");
}

void ExportTrackerFloater::onClickSelectNone()
{
	onChangeFilter("none");
}

void ExportTrackerFloater::onClickSelectObjects()
{
	onChangeFilter("icons/Inv_Object.png");
}

void ExportTrackerFloater::onClickSelectWearables()
{
	onChangeFilter("!object");
}

void ExportTrackerFloater::addAvatarStuff(LLVOAvatar* avatarp)
{
	llinfos << "Getting avatar data..." << llendl;
	LLScrollListCtrl* list = getChild<LLScrollListCtrl>("object_result_list");
	for( S32 type = LLWearableType::WT_SHAPE; type < LLWearableType::WT_COUNT; type++ )
	{
		// guess whether this wearable actually exists
		// by checking whether it has any textures that aren't default
		bool exists = false;
		if(type == LLWearableType::WT_SHAPE)
		{
			exists = true;
		}
		else if (type == LLWearableType::WT_ALPHA || type == LLWearableType::WT_TATTOO) //alpha layers and tattos are unsupported for now
		{
			continue;
		}
		else
		{
			for( S32 te = 0; te < LLVOAvatarDefines::TEX_NUM_INDICES; te++ )
			{
				if( (S32)LLVOAvatarDictionary::getTEWearableType( (LLVOAvatarDefines::ETextureIndex)te ) == type )
				{
					LLViewerTexture* te_image = avatarp->getTEImage( te );
					if( te_image->getID() != IMG_DEFAULT_AVATAR)
					{
						exists = true;
						break;
					}
				}
			}
		}

		if(exists)
		{
			std::string wearable_name = LLWearableType::getTypeName((LLWearableType::EType)type );
			std::string name = avatarp->getFullname() + " " + wearable_name; //LK todoo, fix name

			LLUUID myid;
			myid.generate();

			addObjectToList(
				myid,
				false,
				name,
				wearable_name,
				avatarp->getID());
		}
	}

	// Add attachments
	LLViewerObject::child_list_t child_list = avatarp->getChildren();
	for (LLViewerObject::child_list_t::iterator i = child_list.begin(); i != child_list.end(); ++i)
	{
		LLViewerObject* childp = *i;
		if(list->getItemIndex(childp->getID()) == -1)
		{
			std::string name = "";
			LLSD * props = JCExportTracker::getInstance()->received_properties[childp->getID()];
			if(props != NULL)
			{
				name = std::string((*props)["name"]);
			}

			std::string joint = LLTrans::getString(gAgentAvatarp->getAttachedPointName(childp->getAttachmentItemID()));
			addObjectToList(
				childp->getID(),
				false,
				name + " (worn on " + utf8str_tolower(joint) + ")",
				"Object",
				avatarp->getID());

			LLViewerObject::child_list_t select_list = childp->getChildren();
			LLViewerObject::child_list_t::iterator select_iter;
			int block_counter;

			gMessageSystem->newMessageFast(_PREHASH_ObjectSelect);
			gMessageSystem->nextBlockFast(_PREHASH_AgentData);
			gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
			gMessageSystem->addUUID(_PREHASH_SessionID, gAgent.getSessionID());
			gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
			gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, childp->getLocalID());
			block_counter = 0;
			for (select_iter = select_list.begin(); select_iter != select_list.end(); ++select_iter)
			{
				block_counter++;
				if(block_counter >= 254)
				{
					// start a new message
					gMessageSystem->sendReliable(childp->getRegion()->getHost());
					gMessageSystem->newMessageFast(_PREHASH_ObjectSelect);
					gMessageSystem->nextBlockFast(_PREHASH_AgentData);
					gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
					gMessageSystem->addUUID(_PREHASH_SessionID, gAgent.getSessionID());
				}
				gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
				gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, (*select_iter)->getLocalID());
			}
			gMessageSystem->sendReliable(childp->getRegion()->getHost());

			gMessageSystem->newMessageFast(_PREHASH_ObjectDeselect);
			gMessageSystem->nextBlockFast(_PREHASH_AgentData);
			gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
			gMessageSystem->addUUID(_PREHASH_SessionID, gAgent.getSessionID());
			gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
			gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, childp->getLocalID());
			block_counter = 0;
			for (select_iter = select_list.begin(); select_iter != select_list.end(); ++select_iter)
			{
				block_counter++;
				if(block_counter >= 254)
				{
					// start a new message
					gMessageSystem->sendReliable(childp->getRegion()->getHost());
					gMessageSystem->newMessageFast(_PREHASH_ObjectDeselect);
					gMessageSystem->nextBlockFast(_PREHASH_AgentData);
					gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
					gMessageSystem->addUUID(_PREHASH_SessionID, gAgent.getSessionID());
				}
				gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
				gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, (*select_iter)->getLocalID());
			}
			gMessageSystem->sendReliable(childp->getRegion()->getHost());
		}
	}
}

void ExportTrackerFloater::addObjectToList(LLUUID id, BOOL checked, std::string name, std::string type, LLUUID owner_id)
{
	LLScrollListCtrl* list = getChild<LLScrollListCtrl>("object_result_list");

	LLSD element;
	element["id"] = id;

	LLSD& check_column = element["columns"][LIST_CHECKED];
	check_column["column"] = "Selected";
	check_column["type"] = "checkbox";
	check_column["value"] = checked;

	LLSD& type_column = element["columns"][LIST_TYPE];
	type_column["column"] = "Type";
	type_column["type"] = "icon";
	type_column["value"] = "icons/Inv_" + type + ".png";

	LLSD& name_column = element["columns"][LIST_NAME];
	name_column["column"] = "Name";
	name_column["value"] = name;

	/*
	LLSD& avatarid_column = element["columns"][LIST_AVATARID];
	avatarid_column["column"] = "Avatar ID";
	avatarid_column["value"] = owner_id;
	*/

	list->addElement(element, ADD_BOTTOM);
	list->updateLineHeight();
}

void ExportTrackerFloater::draw()
{
	LLFloater::draw();

	if (JCExportTracker::getInstance()->getStatus() != JCExportTracker::IDLE)	
		refresh();
}

ExportTrackerFloater::~ExportTrackerFloater()
{	
	JCExportTracker::close();
	//which one?? -Patrick Sapinski (Wednesday, November 11, 2009)
	//ExportTrackerFloater::sInstance = NULL;
	//sInstance = NULL;
}

void ExportTrackerFloater::show()
{
	LLFloaterReg::showInstance("export_object_floater");	/*Flawfinder: ignore*/
}

// static
void ExportTrackerFloater::onClickExport()
{
	JCExportTracker::getInstance()->export_tga = getChild<LLCheckBoxCtrl>("export_tga")->get();
	JCExportTracker::getInstance()->export_j2c = getChild<LLCheckBoxCtrl>("export_j2c")->get();
	JCExportTracker::getInstance()->export_properties = getChild<LLCheckBoxCtrl>("export_properties")->get();
	JCExportTracker::getInstance()->export_inventory = getChild<LLCheckBoxCtrl>("export_contents")->get();
	//sInstance->mExportTrees=getChild<LLCheckBoxCtrl>("export_trees")->get();

	JCExportTracker::getInstance()->serialize(mObjectSelection);
}

// static
void ExportTrackerFloater::onClickClose()
{
	closeFloater();
	JCExportTracker::close();
}

JCExportTracker::JCExportTracker()
{
	llassert_always(sInstance == NULL);

	sInstance = this;

}

JCExportTracker::~JCExportTracker()
{
	JCExportTracker::cleanup();
	sInstance = NULL;
}

void JCExportTracker::close()
{
	JCExportTracker::getInstance()->cleanup();
	if(sInstance)
	{
		delete sInstance;
		sInstance = NULL;
	}
}

void JCExportTracker::init(ExportTrackerFloater* export_tracker_instance)
{
	mPropertiesReceived = 0;
	mInventoriesReceived = 0;
	mPropertiesQueries = 0;
	mAssetsExported = 0;
	mTexturesExported = 0;
	mTotalAssets = 0;
	mTotalTextures = 0;
	mTotalPrims = 0;
	mLinksetsExported = 0;
	mTotalLinksets = 0;
	mTotalObjects = 0;
	mExportFloater = export_tracker_instance;
	//mTotalTextures = LLSelectMgr::getInstance()->getSelection()->getTECount(); is this unique textures?

	//you don't touch anything not owned by you if you're following LL's rules, even with mod rights
	//and always use surrogates when not following perms, because I don't care.
#if FOLLOW_PERMS == 1
	using_surrogates = FALSE;
#else
	using_surrogates = FALSE;
#endif

	mStatus = IDLE;
	total = LLSD();
	destination = "";
	asset_dir = "";
	mRequestedTextures.clear();
	processed_prims.clear();
}

LLVOAvatar* find_avatar_from_object( LLViewerObject* object );

LLVOAvatar* find_avatar_from_object( const LLUUID& object_id );



struct JCAssetInfo
{
	std::string path;
	std::string name;
	LLUUID assetid;
	bool export_tga;
	bool export_j2c;
};


LLSD * JCExportTracker::subserialize(LLViewerObject* linkset)
{
	//Chalice - Changed to support exporting linkset groups.
	LLViewerObject* object = linkset;
	//if(!linkset)return LLSD();

	// Create an LLSD object that will hold the entire tree structure that can be serialized to a file
	LLSD * pllsd= new LLSD();
	
	//if (!node)
	//	return llsd;

	//object = root_object = node->getObject();
	
	if (!object)
	{
		cmdline_printchat("subserialize: invalid obj");
		error("", object->getLocalID(), object->getPosition(), "Invalid Object");
		delete pllsd;	
		return NULL;
	}
	if (!(!object->isAvatar()))
	{
		error("", object->getLocalID(), object->getPosition(), "Invalid Object (Avatar)");
		delete pllsd;	
		return NULL;
	}
	if (object->isDead())
	{
		cmdline_printchat("subserialize: object is dead");
		return NULL;
	}

	// Build a list of everything that we'll actually be exporting
	LLDynamicArray<LLViewerObject*> export_objects;

	// Add the root object to the export list
	export_objects.put(object);

	//all child objects must also be active
	llassert_always(object);
	
	// Iterate over all of this objects children
	LLViewerObject::const_child_list_t& child_list = object->getChildren(); //this crashes sometimes. is using llassert a bad hack?? -Patrick Sapinski (Monday, November 23, 2009)
	
	for (LLViewerObject::child_list_t::const_iterator i = child_list.begin(); i != child_list.end(); i++)
	{
		LLViewerObject* child = *i;
		if(!child->isAvatar())
		{
			// Put the child objects on the export list
			export_objects.put(child);
		}
	}
	//deselect everything because the render lag from a large selection really slows down the exporter.
	LLSelectMgr::getInstance()->deselectAll();

	S32 object_index = 0;
	
	while ((object_index < export_objects.count()))
	{
		object = export_objects.get(object_index++);
		LLUUID id = object->getID();
	
		//llinfos << "Exporting prim " << object->getID().asString() << llendl;
	
		// Create an LLSD object that represents this prim. It will be injected in to the overall LLSD
		// tree structure
		LLSD prim_llsd;
	
		if (object_index == 1)
		{
			LLVOAvatar* avatar = find_avatar_from_object(object);
			if (avatar)
			{
				llinfos << "Object is attached to avatar!" << llendl;
				LLViewerJointAttachment* attachment = avatar->getTargetAttachmentPoint(object);
				U8 attachment_id = 0;
				
				if (attachment)
				{
					for (LLVOAvatar::attachment_map_t::iterator iter = avatar->mAttachmentPoints.begin();
										iter != avatar->mAttachmentPoints.end(); ++iter)
					{
						if (iter->second == attachment)
						{
							attachment_id = iter->first;
							break;
						}
					}
				}
				else
				{
					// interpret 0 as "default location"
					attachment_id = 0;
				}
				llinfos << "Adding attachment info." << llendl;
				prim_llsd["Attachment"] = attachment_id;
				prim_llsd["attachpos"] = object->getPosition().getValue();
				llinfos << "getRotation: " << object->getRotation() << ", GetRotationEdit: " << object->getRotationEdit() << llendl;
				prim_llsd["attachrot"] = ll_sd_from_quaternion(object->getRotation());
			}
			
			prim_llsd["position"] = object->getPositionRegion().getValue(); //LLVector3(0, 0, 0).getValue();
			prim_llsd["rotation"] = ll_sd_from_quaternion(object->getRotation());
		}
		else
		{
			prim_llsd["position"] = object->getPositionRegion().getValue();
			prim_llsd["rotation"] = ll_sd_from_quaternion(object->getRotationEdit());
		}
		//prim_llsd["name"] = "";//node->mName;
		//prim_llsd["description"] = "";//node->mDescription;
		// Transforms
		prim_llsd["scale"] = object->getScale().getValue();
		// Flags
		prim_llsd["shadows"] = object->flagCastShadows();
		prim_llsd["phantom"] = object->flagPhantom();
		prim_llsd["physical"] = (BOOL)(object->mFlags & FLAGS_USE_PHYSICS);

		// For saving tree and grass positions store the pcode so we 
		// know what to restore and the state is the species
		LLPCode pcode = object->getPCode();
		prim_llsd["pcode"] = pcode;
		prim_llsd["state"] = object->getState();

		// Only volumes have all the prim parameters
		if(LL_PCODE_VOLUME == pcode) 
		{
			LLVolumeParams params = object->getVolume()->getParams();
			prim_llsd["volume"] = params.asLLSD();

			if (object->isFlexible())
			{
				LLFlexibleObjectData* flex = (LLFlexibleObjectData*)object->getParameterEntry(LLNetworkData::PARAMS_FLEXIBLE);
				prim_llsd["flexible"] = flex->asLLSD();
			}
			if (object->getParameterEntryInUse(LLNetworkData::PARAMS_LIGHT))
			{
				LLLightParams* light = (LLLightParams*)object->getParameterEntry(LLNetworkData::PARAMS_LIGHT);
				prim_llsd["light"] = light->asLLSD();
			}
			if (object->getParameterEntryInUse(LLNetworkData::PARAMS_SCULPT))
			{

				LLSculptParams* sculpt = (LLSculptParams*)object->getParameterEntry(LLNetworkData::PARAMS_SCULPT);
				prim_llsd["sculpt"] = sculpt->asLLSD();

				if(export_tga || export_j2c)
				{
					LLFile::mkdir(asset_dir+"//sculptmaps//");
					std::string path = asset_dir+"//sculptmaps//";
					LLUUID asset_id = sculpt->getSculptTexture();
					asset_id = check_texture_perm(asset_id,true);
					JCAssetInfo* info = new JCAssetInfo;
					info->path = path + asset_id.asString();
					info->name = "Sculpt Texture";
					info->assetid = asset_id;
					info->export_tga = export_tga;
					info->export_j2c = export_j2c;
					if(mRequestedTextures.count(asset_id) == 0)
					{
						mTotalTextures++;
						mRequestedTextures.insert(asset_id);
						LLViewerFetchedTexture* img = LLViewerTextureManager::getFetchedTexture(asset_id);
						
						img->setBoostLevel(LLViewerFetchedTexture::BOOST_MAX_LEVEL);
						img->addTextureStats( (F32)MAX_IMAGE_AREA );
						img->forceToSaveRawImage(0); //this is required for us to receive the full res image.
						img->setLoadedCallback( JCExportTracker::onFileLoadedForSave, 0, TRUE, FALSE, info, NULL );
						mTexturesExported++;
						mExportFloater->refresh();
					}
				}
			}
		}

		// Textures
		LLSD te_llsd;
		U8 te_count = object->getNumTEs();
		
		for (U8 i = 0; i < te_count; i++)
		{
			te_llsd.append(object->getTE(i)->asLLSD());
		}

		if(export_tga || export_j2c)
		{
			LLFile::mkdir(asset_dir+"//textures//");
			std::string path = asset_dir+"//textures//";
			for (U8 i = 0; i < te_count; i++)
			{
				LLUUID asset_id = object->getTE(i)->getID();
				asset_id = check_texture_perm(asset_id,false);
				JCAssetInfo* info = new JCAssetInfo;
				info->path = path + asset_id.asString();
				info->name = "Prim Texture";
				info->assetid = asset_id;
				info->export_tga = export_tga;
				info->export_j2c = export_j2c;
				//gAssetStorage->getAssetData(asset_id, LLAssetType::AT_TEXTURE, JCAssetExportCallback, info,1);
				if(mRequestedTextures.count(asset_id) == 0)
				{
					JCExportTracker::mTotalTextures++;
					mRequestedTextures.insert(asset_id);
					LLViewerFetchedTexture* img = LLViewerTextureManager::getFetchedTexture(asset_id);
					
						img->setBoostLevel(LLViewerFetchedTexture::BOOST_MAX_LEVEL);
						img->addTextureStats( (F32)MAX_IMAGE_AREA );
						img->forceToSaveRawImage(0); //this is required for us to receive the full res image. (snowglobe)
						img->setLoadedCallback( JCExportTracker::onFileLoadedForSave, 0, TRUE, FALSE, info, NULL );
						mTexturesExported++;
						mExportFloater->refresh();
				}
			}
		}

		//JCExportTracker::mirror(asset, obj, asset_dir, asset->getUUID().asString());
		
		prim_llsd["textures"] = te_llsd;

		prim_llsd["id"] = object->getID().asString();

		prim_llsd["inventoryReceived"]=object->mInventoryRecieved;

		mTotalPrims++;

		// Changed to use link numbers zero-indexed.
		(*pllsd)[object_index - 1] = prim_llsd;
	}

	return pllsd;
}


void JCExportTracker::error(std::string name, U32 localid, LLVector3 object_pos, std::string error_msg)
{
	std::stringstream sstr;	
	sstr <<llformat("%.1f", object_pos.mV[VX]);
	sstr <<","<<llformat("%.1f", object_pos.mV[VY]);
	sstr <<","<<llformat("%.1f", object_pos.mV[VZ]);

	if (mExportFloater)
	{
		//add to error list
		LLSD element;
		element["id"] = llformat("%u",localid);

		element["columns"][0]["column"] = "Name";
		element["columns"][0]["type"] = "text";
		element["columns"][0]["value"] = name;

		element["columns"][1]["column"] = "Local ID";
		element["columns"][1]["type"] = "text";
		element["columns"][1]["value"] = llformat("%u",localid);

		element["columns"][2]["column"] = "Position";
		element["columns"][2]["type"] = "text";
		element["columns"][2]["value"] = sstr.str();

		element["columns"][3]["column"] = "Error Message";
		element["columns"][3]["type"] = "text";
		element["columns"][3]["value"] = error_msg;


		LLScrollListCtrl* mResultList;
		mResultList = mExportFloater->getChild<LLScrollListCtrl>("error_result_list");
		mResultList->addElement(element, ADD_BOTTOM);
	}
	else
		cmdline_printchat("error:" + error_msg);
}


bool JCExportTracker::getAsyncData(LLViewerObject * obj)
{
	LLPCode pcode = obj->getPCode();
	if(pcode!=LL_PCODE_VOLUME)
		return false;
	//if(export_properties) //Causes issues with precaching the properties...
	{
		bool already_requested_prop=false;
		/*if (obj->mPropertiesRecieved)
		{
			llinfos << "Skipping requesting properties on " << obj->getID() << ", we already have them." << llendl;
			already_requested_prop = true;
		}*/
		std::list<PropertiesRequest_t *>::iterator iter=requested_properties.begin();
		for(;iter!=requested_properties.end();iter++)
		{
			PropertiesRequest_t * req=(*iter);
			if(req->localID==obj->getLocalID())
			{
				//cmdline_printchat("BREAK: have properties for: " + llformat("%d",obj->getLocalID()));
				already_requested_prop = true;
				break;	
			}
		}
			
		if(already_requested_prop == false)
		{
			//save this request to the list
			PropertiesRequest_t * req = new PropertiesRequest_t();
			req->target_prim=obj->getID();
			req->request_time=0;
			req->num_retries = 0;	
			req->localID=obj->getLocalID();
			requested_properties.push_back(req);
			//cmdline_printchat("queued property request for: " + llformat("%d",obj->getLocalID()));

		}

		if(export_inventory && (!using_surrogates || obj->permYouOwner()))
		{

			bool already_requested_inv=false;
			std::list<InventoryRequest_t *>::iterator iter2=requested_inventory.begin();
			for(;iter2!=requested_inventory.end();iter2++)
			{
				InventoryRequest_t * req=(*iter2);
				if(req->real_object->getLocalID()==obj->getLocalID())
				{
					//cmdline_printchat("BREAK already have inventory for: " + llformat("%d",obj->getLocalID()));
					already_requested_inv=true;
					break;
				}
			}

			if(!already_requested_inv)
			{
				requestInventory(obj);
			}
			else
			{
				llinfos << "We already have the inventory for this or something" << llendl;
			}
		}
	}
	return true;
}

void JCExportTracker::requestInventory(LLViewerObject * obj, LLViewerObject * surrogate_obj)
{
	InventoryRequest_t * ireq = new InventoryRequest_t();

	if(surrogate_obj != NULL)
		ireq->object=surrogate_obj;
	else
		ireq->object=obj;

	ireq->real_object=obj; //sometimes we need to know who this inventory is really intended for
	ireq->request_time = 0;
	ireq->num_retries = 0;
	requested_inventory.push_back(ireq);

	if(surrogate_obj != NULL)
	{
		obj->mInventoryRecieved=false;
		surrogate_obj->mInventoryRecieved=false;
		surrogate_obj->registerInventoryListener(sInstance,(void*)ireq);
	}
	else
	{
		obj->mInventoryRecieved=false;
		obj->registerInventoryListener(sInstance,(void*)ireq);
	}
}

BOOL JCExportTracker::processSurrogate(LLViewerObject *surrogate_object)
{
	LLUUID target_id = expected_surrogate_pos[surrogate_object->getPosition()];
	LLViewerObject * source_object = gObjectList.findObject(target_id);

	if(!source_object)
	{
		llwarns << "Couldn't find the source object!" << llendl;
		return FALSE;
	}

	llinfos << "Number of children: " << source_object->numChildren() << llendl;
	llinfos << "Number of surrogate children: " << surrogate_object->numChildren() << llendl;

	//request the inventory for any child prims as well, if there are any
	if(source_object->numChildren() > 0)
	{
		LLViewerObject::child_list_t child_list = source_object->getChildren();
		LLViewerObject::child_list_t surrogate_children = surrogate_object->getChildren();

		if(child_list.size() != surrogate_children.size())
		{
			llwarns << "There aren't as many links in the source object as in the surrogate? Aborting." << llendl;
			return FALSE;
		}

		LLViewerObject::child_list_t::const_iterator surrogate_link = surrogate_children.begin();

		for (LLViewerObject::child_list_t::const_iterator i = child_list.begin(); i != child_list.end(); i++)
		{
			LLViewerObject* child = *i;
			LLViewerObject* surrogate_child = *surrogate_link++;

			llinfos << child->mLocalID << " : " << surrogate_child->mLocalID << llendl;

			requestInventory(child, surrogate_child);
		}
	}

	requestInventory(source_object, surrogate_object);

	expected_surrogate_pos.erase(surrogate_object->getPosition());

	return TRUE;
}

void JCExportTracker::createSurrogate(LLViewerObject *object)
{
	gMessageSystem->newMessageFast(_PREHASH_ObjectDuplicate);
	gMessageSystem->nextBlockFast(_PREHASH_AgentData);
	gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
	gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
	gMessageSystem->addUUIDFast(_PREHASH_GroupID, gAgent.getGroupID());
	gMessageSystem->nextBlockFast(_PREHASH_SharedData);

	//make the magic rez position so we know this
	LLVector3 rezpos = gAgent.getPositionAgent() + LLVector3(0.0f, 0.0f, 2.0f) +
					   LLVector3(ll_frand(), ll_frand(), ll_frand());

	llinfos << "Creating the surrogate at " << rezpos << llendl;

	expected_surrogate_pos[rezpos] = object->mID;

	rezpos -= object->getPositionRegion();

	gMessageSystem->addVector3Fast(_PREHASH_Offset, rezpos);
	//the two is important, it will set the select on creation flag for
	//the prim, which we will catch in the object update once it's created
	gMessageSystem->addU32Fast(_PREHASH_DuplicateFlags, 2);
	gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
	gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, object->getLocalID());
	gMessageSystem->sendReliable(gAgent.getRegionHost());
}

void JCExportTracker::removeSurrogates()
{
	llinfos << "Removing " << surrogate_roots.size() << " surrogate objects." << llendl;
	std::list<LLViewerObject *>::iterator iter_surr=surrogate_roots.begin();
	for(;iter_surr!=surrogate_roots.end();iter_surr++)
	{
		removeObject((*iter_surr));
	}

	surrogate_roots.clear();
}

void JCExportTracker::removeObject(LLViewerObject* obj)
{
	gMessageSystem->newMessageFast(_PREHASH_ObjectDelete);
	gMessageSystem->nextBlockFast(_PREHASH_AgentData);
	gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
	gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
	gMessageSystem->addBOOLFast(_PREHASH_Force, FALSE);
	gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
	gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, obj->getLocalID());

	gMessageSystem->sendReliable(gAgent.getRegionHost());
}

void JCExportTracker::requestPrimProperties(U32 localID)
{
	gMessageSystem->newMessageFast(_PREHASH_ObjectSelect);
	gMessageSystem->nextBlockFast(_PREHASH_AgentData);
	gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
	gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
	gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, localID);

    gMessageSystem->sendReliable(gAgent.getRegionHost());
}

class CacheReadResponder : public LLTextureCache::ReadResponder
{
private:
	LLPointer<LLImageFormatted> mCacheImage;
	LLUUID mID;
	std::string mFolder;

public:
	CacheReadResponder(const LLUUID& id, LLImageFormatted* image, std::string &folder) 
	  : mCacheImage(image),
		mID(id),
		mFolder(folder)
	{
		setImage(image);
	}

	virtual void completed(bool success)
	{
		if(success) {
			if(mCacheImage.notNull() && mImageSize > 0) {
				if(!mCacheImage->save(mFolder))
					llwarns << "Could not save fetched texture" << llendl;
			}
			else
				llwarns << "Error on fetching texture from cache" << llendl;
		}
		else
			llwarns << "Fetching texture from cache could not complete" << llendl;
	}

	void setData(U8* data, S32 datasize, S32 imagesize, S32 imageformat, BOOL imagelocal)
	{
		if(imageformat == IMG_CODEC_TGA) {
			llwarns << "Fetching texture is IMG_CODEC_TGA not j2c" << llendl;
			mFormattedImage = NULL;
			mImageSize = 0;
			return;
		}
		
		//copy'd from LLTextureCache::ReadResponder::setData
		if (mFormattedImage.notNull())
		{
			llassert_always(mFormattedImage->getCodec() == imageformat);
			mFormattedImage->appendData(data, datasize);
		}
		else
		{
			mFormattedImage = LLImageFormatted::createFromType(imageformat);
			mFormattedImage->setData(data,datasize);
		}
		mImageSize = imagesize;
		mImageLocal = imagelocal;
	}
};

LLUUID JCExportTracker::check_texture_perm(LLUUID id, bool isSculpt)
{
#if FOLLOW_PERMS == 0
	return id;
#endif

	if(!(LLGridManager::getInstance()->isInSLMain() || LLGridManager::getInstance()->isInSLBeta())) {
		//is NOT Second Life
		return id;
	}

	//allow some default textures
	if(id == LLUUID("5748decc-f629-461c-9a36-a35a221fe21f") || //blank
	   id == LLUUID("38b86f85-2575-52a9-a531-23108d8da837") || //invisible
	   id == LLUUID("8dcd4a48-2d37-4909-9f78-f7a9eb4ef903") || //transparent
	   id == LLUUID("8b5fec65-8d8d-9dc5-cda8-8fdf2716e361") )  //media
	{
		return id;
	}

	//get permissions via inventory search
	LLUUID itemID;
	LLViewerInventoryCategory::cat_array_t cats;
	LLViewerInventoryItem::item_array_t items;
	LLAssetIDMatches idMatch(id);
	gInventory.collectDescendentsIf(LLUUID::null, cats, items, LLInventoryModel::INCLUDE_TRASH, idMatch);
	for (S32 iIndex = 0; iIndex < items.count(); iIndex++)
	{
		LLPermissions perm = items[iIndex]->getPermissions();
		if(exportAllowed(&perm)) {
			itemID = id;
			break;
		}
	}
	
	if (itemID.notNull()) return id;

	//no permission, use default
	llinfos << "No permission to export texture, using default texture" << llendl;
	if(!isSculpt)
		return LLUUID(gSavedSettings.getString("DefaultObjectTexture"));
	else
		return LLUUID("5748decc-f629-461c-9a36-a35a221fe21f"); //blank
}

void JCExportTracker::onFileLoadedForSave(BOOL success, 
											LLViewerFetchedTexture *src_vi,
											LLImageRaw* src, 
											LLImageRaw* aux_src, 
											S32 discard_level,
											BOOL final,
											void* userdata)
{
	JCAssetInfo* info = (JCAssetInfo*)userdata;
	if(final)
	{
		if( success )
		{ /*
			LLPointer<LLImageJ2C> image_j2c = new LLImageJ2C();
			if(!image_j2c->encode(src,0.0))
			{
				//errorencode
				llinfos << "Failed to encode " << info->path << llendl;
			}else if(!image_j2c->save( info->path ))
			{
				llinfos << "Failed to write " << info->path << llendl;
				//errorwrite
			}else
			{
				ExportTrackerFloater::mTexturesExported++;
				llinfos << "Saved texture " << info->path << llendl;
				//success
			} */
			
			//RC
			//If we have a NULL raw image, then read one back from the GL buffer
			if(src==NULL)
			{
				src = src_vi->reloadRawImage(0);
				if(!src)
				{
					cmdline_printchat("Failed to readback texture");
					delete info;
					return;
				}
				if(src_vi->getRawImageLevel() != 0) {
					//if this happens, whe might need a list for textures waiting to get discard_level 0
					delete info;
					return;
				}
			}
			
			if(info->export_tga)
			{
				LLPointer<LLImageTGA> image_tga = new LLImageTGA;

				if( !image_tga->encode( src ) )
				{
					llinfos << "Failed to encode " << info->path << llendl;
				}
				else if( !image_tga->save( info->path + ".tga" ) )
				{
					llinfos << "Failed to write " << info->path << llendl;
				}
			}

			if(info->export_j2c)
			{
				LLPointer<LLImageJ2C> image_j2c = new LLImageJ2C();

				/*
				if(src->getWidth() * src->getHeight() <= LL_IMAGE_REZ_LOSSLESS_CUTOFF * LL_IMAGE_REZ_LOSSLESS_CUTOFF)
					image_j2c->setReversible(TRUE);

				if(!image_j2c->encode(src,0.0))
				{
					//errorencode
					llinfos << "Failed to encode " << info->path << llendl;
				}else if(!image_j2c->save( info->path+".jp2" ))
				{
					llinfos << "Failed to write " << info->path << llendl;
					//errorwrite
				}else
				{
					//llinfos << "Saved texture " << info->path << llendl;
					//success
				}
				*/

				//getting j2c straight from cache, no re-encoding losses
				std::string image_path = info->path + ".jp2";
				CacheReadResponder* responder = new CacheReadResponder(info->assetid, (LLImageFormatted *)image_j2c, image_path);
				LLAppViewer::getTextureCache()->readFromCache(info->assetid, LLWorkerThread::PRIORITY_HIGH, 0, S32_MAX, responder);
			}
		}

		delete info;
	}

}

void JCExportTracker::exportTexture(LLUUID assetid, std::string path, std::string name)
{
	LLUUID val_id = check_texture_perm(assetid, false);
	if(val_id != assetid) {
		LL_INFOS("JCExportTracker") << "Export texture bad perm " << assetid << LL_ENDL;
		return;
	}

	LLPointer<LLViewerFetchedTexture> imagep = LLViewerTextureManager::getFetchedTexture(assetid, MIPMAP_TRUE, LLViewerTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
		if (imagep.notNull())
		{
			S32 cur_discard = imagep->getDiscardLevel();
			if (cur_discard > 0)
			{
				if (imagep->getBoostLevel() != LLViewerTexture::BOOST_PREVIEW)
				{
					// we want to force discard 0: this one does this.
					imagep->setBoostLevel(LLViewerTexture::BOOST_MAX_LEVEL);
					imagep->forceToSaveRawImage(0);
				}
			}
		}
		else
	{
		LL_WARNS("JCExportTracker") << "We *DON'T* have the texture " << assetid.asString() << LL_ENDL;
		return;
	}

	JCAssetInfo* info = new JCAssetInfo;
	info->path = path + "//" + name;
	info->name = "AV Texture";
	info->assetid = assetid;
	info->export_tga = false;
	info->export_j2c = true;
	imagep->setLoadedCallback( JCExportTracker::onFileLoadedForSave, 0, TRUE, FALSE, info, NULL );
}

bool JCExportTracker::serializeSelection()
{
	//init();
	LLDynamicArray<LLViewerObject*> objectArray;
	for (LLObjectSelection::valid_root_iterator iter = LLSelectMgr::getInstance()->getSelection()->valid_root_begin();
			 iter != LLSelectMgr::getInstance()->getSelection()->valid_root_end(); iter++)
	{
		LLSelectNode* selectNode = *iter;
		LLViewerObject* object = selectNode->getObject();
		if(object)objectArray.put(object);
	}
	return serialize(objectArray);
}

bool JCExportTracker::serialize(LLDynamicArray<LLViewerObject*> objects)
{
	LLSelectMgr::getInstance()->deselectAll();

	llwarns << "serialize beginning export !! " << llendl;

	LLFilePicker& file_picker = LLFilePicker::instance();
		
	if (destination == "")
	{
		if (!file_picker.getSaveFile(LLFilePicker::FFSAVE_HPA))
			return false; // User canceled save.
		destination = file_picker.getFirstFile();
	}
	mStatus = EXPORTING;

	mExportFloater->childSetEnabled("export",false);


	asset_dir = gDirUtilp->getDirName(destination);

	//destination = destination.substr(0,destination.find_last_of("."));
/*	if(export_inventory)
	{
		asset_dir = destination.substr(0,destination.find_last_of(".")) + "_assets";//+gDirUtilp->getDirDelimiter();
		if(!LLFile::isdir(asset_dir))
		{
			LLFile::mkdir(asset_dir);
		}else
		{
			U32 response = OSMessageBox("Directory "+asset_dir+" already exists, write on top?", "Overwrite?", OSMB_YESNO);
			if(response)
			{

				obj->	return false;
			}
		}
	} */

	total.clear();

	LLDynamicArray<LLViewerObject*>::const_iterator iter_obj = objects.begin();
#if FOLLOW_PERMS == 0
	if(using_surrogates)
	{
		for (;iter_obj != objects.end(); iter_obj++)
		{
			if(!(*iter_obj)->permYouOwner() && (*iter_obj)->permModify())
				createSurrogate((*iter_obj));
		}
	}
#endif
	gIdleCallbacks.addFunction(exportworker, NULL);

	return true;

}

void JCExportTracker::exportworker(void *userdata)
{
	JCExportTracker::getInstance()->doexportworker(userdata);
}
void JCExportTracker::doexportworker(void *userdata)
{
	//CHECK IF WE'RE DONE
	if(ExportTrackerFloater::mObjectSelection.empty() && ExportTrackerFloater::mObjectSelectionWaitList.empty()
		&& (requested_inventory.empty())// ||gSavedSettings.getBOOL("ExporterInventoryLater"))
		&& requested_properties.empty() && expected_surrogate_pos.empty())
	{
		gIdleCallbacks.deleteFunction(JCExportTracker::exportworker);
		removeSurrogates();
		finalize();
		return;
	}

	if(using_surrogates && queued_surrogates.size() > 0)
	{
		llinfos << queued_surrogates.size() << " surrogates to check" << llendl;

		std::deque<LLViewerObject *>::iterator iter_end = queued_surrogates.end();

		//check all of our surrogates that are queued for requests and make the request if they are ready
		for (std::deque<LLViewerObject *>::iterator i = queued_surrogates.begin(); !queued_surrogates.empty() && i != iter_end; i++)
		{
			if(*i != NULL && processSurrogate(*i))
				queued_surrogates.pop_front();
		}
	}
	
	int kick_count=0;
	int total=requested_properties.size();
	// Check for requested properties that are taking too long
	while(total!=0)
	{
		std::list<PropertiesRequest_t*>::iterator iter;
		time_t tnow=time(NULL);
		
		PropertiesRequest_t * req;
		iter=requested_properties.begin();
		req=(*iter);
		requested_properties.pop_front();
		
		if( (req->request_time+PROP_REQUEST_KICK)< tnow)
		{
			req->request_time=time(NULL);
			req->num_retries++;

			requestPrimProperties(req->localID);
			kick_count++;
			//cmdline_printchat("requested property for: " + llformat("%d",req->localID));
		}
		if (req->num_retries < gSavedSettings.getU32("ExporterNumberRetrys"))
			requested_properties.push_back(req);
		else
		{
			//req->localID->mPropertiesRecieved = true;		
			cmdline_printchat("failed to retrieve properties for ");// + req->localID);
			//mPropertiesReceived++;
			LLViewerObject *obj = gObjectList.findObject(req->target_prim);
			if(obj)
			{
				obj->mPropertiesRecieved=true;
				//cmdline_printchat("successfully set property to received");
			}
		}
		
		if(kick_count>=10)
			break; // that will do for now				

		total--;
	}

	kick_count=0;
	total=requested_inventory.size();

	llinfos << "Inventory requests left: " << total << llendl;

	// Check for inventory properties that are taking too long
	std::list<InventoryRequest_t*> still_going;
	still_going.clear();
	while(total!=0)
	{
		std::list<InventoryRequest_t*>::iterator iter;
		time_t tnow=time(NULL);
		
		InventoryRequest_t * req;
		iter=requested_inventory.begin();
		req=(*iter);
		requested_inventory.pop_front();
		
		if( (req->request_time+INV_REQUEST_KICK)< tnow)
		{
			req->request_time=time(NULL);
			if(ExportTrackerFloater::mObjectSelection.empty())//dont count inv retrys till things calm down a bit
			{
				req->num_retries++;
				//cmdline_printchat("Re-trying inventory request for " + req->object->mID.asString()+
				//	", try number "+llformat("%u",req->num_retries));
			}

			//LLViewerObject* obj = gObjectList.findObject(req->object->mID);
			LLViewerObject* obj = req->object;

			//cmdline_printchat("requesting inventory: " + obj->mID.asString());

			if(obj->mID.isNull())
				cmdline_printchat("no mID");

			if (!obj)
			{
				cmdline_printchat("export worker: " + req->object->mID.asString() + " not found in object list");
			}
			LLPCode pcode = obj->getPCode();
			if (pcode != LL_PCODE_VOLUME &&
				pcode != LL_PCODE_LEGACY_GRASS &&
				pcode != LL_PCODE_LEGACY_TREE)
			{
				cmdline_printchat("exportworker: object has invalid pcode");
				//return NULL;
			}
			obj->removeInventoryListener(sInstance);
			sInstance->registerVOInventoryListener(obj,NULL);
			obj->dirtyInventory();
			sInstance->requestVOInventory();

			//obj->dirtyInventory();
			//obj->requestInventory();
			//kick_count++;
		}
		kick_count++;

		if (req->num_retries < gSavedSettings.getU32("ExporterNumberRetrys"))
			still_going.push_front(req);
		else
		{
			req->object->mInventoryRecieved = true;
			req->real_object->mInventoryRecieved=true;

			//object->removeInventoryListener(sInstance);
			//obj->mInventoryRecieved=true;
			cmdline_printchat("failed to retrieve inventory for " + req->object->mID.asString());
			mInventoriesReceived++;
		}

		
		if(kick_count>=gSavedSettings.getS32("ExporterNumberConcurentDownloads"))
			break; // that will do for now				

		total--;
	}

	while(!still_going.empty())
	{
		std::list<InventoryRequest_t*>::iterator iter;
		InventoryRequest_t * req;
		iter=still_going.begin();
		req=(*iter);
		still_going.pop_front();
		requested_inventory.push_back(req);
	}

	int count=0;
	LLScrollListCtrl* list = mExportFloater->getChild<LLScrollListCtrl>("object_result_list");
	std::vector<LLScrollListItem*> items = list->getAllData();
	//Keep the requested list topped up
	while(count < 20)
	{
		if(!ExportTrackerFloater::mObjectSelection.empty())
		{
			LLViewerObject* obj=ExportTrackerFloater::mObjectSelection.get(0);
			ExportTrackerFloater::mObjectSelection.remove(0);

			if (!obj)
			{
				cmdline_printchat("export worker (getasyncdata): invalid obj");
				break;
			}
			std::string name = "";
			LLSD * props = received_properties[obj->getID()];
			if(props != NULL)
			{
				name = std::string((*props)["name"]);
			}

			LLSD element;
			
			LLScrollListItem* itemp = list->getItem(obj->getID());
			if (!itemp)
			{
				llwarns << "ERROR: Could not find " << name << " in list." << llendl;
				//return;
			}

			else if (!itemp->getColumn(0)->getValue().asBoolean())
			{
				llinfos << "Object " << name << " is not selected for export, skipping." << llendl;
				break;
			}

			/*
			if (itemp)
				itemp->getColumn(4)->setValue("OK");
			*/

			ExportTrackerFloater::mObjectSelectionWaitList.push_back(obj);
			// We need to go off and get properties, inventorys and textures now
			getAsyncData(obj);
			//cmdline_printchat("getAsyncData for: " + llformat("%d",obj->getLocalID()));
			count++;

			//all child objects must also be active
			llassert_always(obj);
			LLViewerObject::child_list_t child_list = obj->getChildren();
			if(!child_list.empty())
			for (LLViewerObject::child_list_t::const_iterator i = child_list.begin(); i != child_list.end(); i++)
			{
				LLViewerObject* child = *i;
				getAsyncData(child);
				//cmdline_printchat("getAsyncData for: " + llformat("%d",child->getLocalID()));
			}
		}
		else
		{
			break;
		}
	}

	LLDynamicArray<LLViewerObject*>::iterator iter=ExportTrackerFloater::mObjectSelectionWaitList.begin();
	
	LLViewerObject* object = NULL;

	for(;iter!=ExportTrackerFloater::mObjectSelectionWaitList.end();iter++)
	{
		//Find an object that has completed export
		
		if(((export_properties==FALSE) || (*iter)->mPropertiesRecieved) && 
			( ((export_inventory==FALSE) || (*iter)->mInventoryRecieved)))//|| gSavedSettings.getBOOL("ExporterInventoryLater") )
		{
			//we have the root properties and inventory now check all children
			bool got_all_stuff=true;
			LLViewerObject::child_list_t child_list;
			child_list = (*iter)->getChildren();
			llassert(!child_list.empty());
			for (LLViewerObject::child_list_t::const_iterator i = child_list.begin(); i != child_list.end(); i++)
			{
				LLViewerObject* child = *i;
				if(!child->isAvatar())
				{
					if( ((export_properties==TRUE) && (!child->mPropertiesRecieved)) || ((export_inventory==TRUE) && !child->mInventoryRecieved))
					{
						got_all_stuff=false;
						getAsyncData(child);
						//cmdline_printchat("Export stalled on object " + llformat("%d",child->getLocalID()));
						llwarns<<"Export stalled on object "<<child->getID()<<" local "<<child->getLocalID()<<llendl;
						break;
					}
				}
			}

			if(got_all_stuff==false)
				continue;

			object=(*iter);		
			LLVector3 object_pos = object->getPosition();
			LLSD origin;
			origin["ObjectPos"] = object_pos.getValue();
			
			//meh fix me, why copy the LLSD here, better to use just the pointer
			LLSD * plinkset = subserialize(object); 

			if(plinkset==NULL || (*plinkset).isUndefined())
			{
				cmdline_printchat("Object returned undefined linkset");				
			}
			else
			{
				processed_prims.push_back(plinkset);
			}
			
			mLinksetsExported++;
			//Erase this entry from the wait list then BREAK, do not loop
			ExportTrackerFloater::mObjectSelectionWaitList.erase(iter);
			break;	
		}
	}
}

void JCExportTracker::propertyworker(void *userdata)
{
	JCExportTracker::getInstance()->dopropertyworker(userdata);
}
void JCExportTracker::dopropertyworker(void *userdata)
{
	LLScrollListCtrl* list = mExportFloater->getChild<LLScrollListCtrl>("object_result_list");

	if(requested_properties.empty())
	{
		llinfos << "Property worker done, updating object list and closing." << llendl;
		gIdleCallbacks.deleteFunction(propertyworker);
		mExportFloater->buildListDisplays();
		return;
	}
	
	int kick_count=0;
	int total = requested_properties.size();
	// Check for requested properties that are taking too long
	while(total!=0)
	{
		std::list<PropertiesRequest_t*>::iterator iter;
		time_t tnow=time(NULL);
		
		PropertiesRequest_t * req;
		iter=requested_properties.begin();
		req=(*iter);
		requested_properties.pop_front();
		
		if( (req->request_time+PROP_REQUEST_KICK) < tnow)
		{
			req->request_time=time(NULL);
			req->num_retries++;

			requestPrimProperties(req->localID);
			kick_count++;
			//cmdline_printchat("requested property for: " + llformat("%d",req->localID));
		}
		if (req->num_retries < gSavedSettings.getU32("ExporterNumberRetrys"))
			requested_properties.push_back(req);
		else
		{
			//req->localID->mPropertiesRecieved = true;		
			cmdline_printchat("failed to retrieve properties for ");// + req->localID);
			//mPropertiesReceived++;
			LLViewerObject *obj = gObjectList.findObject(req->target_prim);
			if(obj)
			{
				obj->mPropertiesRecieved = true;
				LLScrollListItem* itemp = list->getItem(obj->getID());
				if (!itemp)
				{
					llwarns << "ERROR: Could not find " << obj->getID() << " in list." << llendl;
					break;
				}
			}
		}
		
		if(kick_count>=20)
			break; // that will do for now				

		total--;
	}
}

void JCExportTracker::finalize()
{
	//We convert our LLSD to HPA here.

	LLXMLNode *project_xml = new LLXMLNode("project", FALSE);
														
	project_xml->createChild("schema", FALSE)->setValue("1.0");
	project_xml->createChild("name", FALSE)->setValue(gDirUtilp->getBaseFileName(destination, false));
	project_xml->createChild("date", FALSE)->setValue(LLLogChat::timestamp(1));
	project_xml->createChild("software", FALSE)->setValue(llformat("%s %d.%d.%d",
	LLAppViewer::instance()->getSecondLifeTitle().c_str(), LLVersionInfo::getMajor(), LLVersionInfo::getMinor(), LLVersionInfo::getPatch()));
	std::vector<std::string> uris;
	
	LLSD grid_info;
	LLGridManager::getInstance()->getGridData(grid_info);
	std::string grid_login = grid_info[GRID_LOGIN_URI_VALUE];
	std::string grid_platform = grid_info[GRID_PLATFORM];

	project_xml->createChild("grid", FALSE)->setValue(grid_login);
	
	if(LLGridManager::getInstance()->isInSLMain()) {
		project_xml->createChild("platform", FALSE)->setValue(AGNI);
	}
	else if (LLGridManager::getInstance()->isInSLBeta()) {
		project_xml->createChild("platform", FALSE)->setValue(ADITI);
	}
	else {
		project_xml->createChild("platform", FALSE)->setValue(grid_platform);
	}

	LLXMLNode *group_xml;
	group_xml = new LLXMLNode("group",FALSE);

	LLVector3 max = selection_center + selection_size / 2;
	LLVector3 min = selection_center - selection_size / 2;

	LLXMLNodePtr max_xml = group_xml->createChild("max", FALSE);
	max_xml->createChild("x", TRUE)->setValue(llformat("%.5f", max.mV[VX]));
	max_xml->createChild("y", TRUE)->setValue(llformat("%.5f", max.mV[VY]));
	max_xml->createChild("z", TRUE)->setValue(llformat("%.5f", max.mV[VZ]));

	LLXMLNodePtr min_xml = group_xml->createChild("min", FALSE);
	min_xml->createChild("x", TRUE)->setValue(llformat("%.5f", min.mV[VX]));
	min_xml->createChild("y", TRUE)->setValue(llformat("%.5f", min.mV[VY]));
	min_xml->createChild("z", TRUE)->setValue(llformat("%.5f", min.mV[VZ]));
											
	LLXMLNodePtr center_xml = group_xml->createChild("center", FALSE);
	center_xml->createChild("x", TRUE)->setValue(llformat("%.5f", selection_center.mV[VX]));
	center_xml->createChild("y", TRUE)->setValue(llformat("%.5f", selection_center.mV[VY]));
	center_xml->createChild("z", TRUE)->setValue(llformat("%.5f", selection_center.mV[VZ]));

	//cmdline_printchat("Attempting to output " + llformat("%u", data.size()) + " Objects.");

	std::list<LLSD *>::iterator iter=processed_prims.begin();
	for(;iter!=processed_prims.end();iter++)
	{	// for each object
			
		LLXMLNode *linkset_xml = new LLXMLNode("linkset", FALSE);
		LLSD *plsd=(*iter);
		//llwarns<<LLSD::dump(*plsd)<<llendl;
		for(LLSD::array_iterator link_itr = plsd->beginArray();
			link_itr != plsd->endArray();
			++link_itr)
		{ 
				LLSD prim = (*link_itr);
				
				std::string selected_item	= "box";
				F32 scale_x=1.f, scale_y=1.f;
			
				LLVolumeParams volume_params;
				volume_params.fromLLSD(prim["volume"]);
				// Volume type
				U8 path = volume_params.getPathParams().getCurveType();
				U8 profile_and_hole = volume_params.getProfileParams().getCurveType();
				U8 profile	= profile_and_hole & LL_PCODE_PROFILE_MASK;
				U8 hole		= profile_and_hole & LL_PCODE_HOLE_MASK;
				
				// Scale goes first so we can differentiate between a sphere and a torus,
				// which have the same profile and path types.
				// Scale
				scale_x = volume_params.getRatioX();
				scale_y = volume_params.getRatioY();
				BOOL linear_path = (path == LL_PCODE_PATH_LINE) || (path == LL_PCODE_PATH_FLEXIBLE);
				if ( linear_path && profile == LL_PCODE_PROFILE_CIRCLE )
				{
					selected_item = "cylinder";
				}
				else if ( linear_path && profile == LL_PCODE_PROFILE_SQUARE )
				{
					selected_item = "box";
				}
				else if ( linear_path && profile == LL_PCODE_PROFILE_ISOTRI )
				{
					selected_item = "prism";
				}
				else if ( linear_path && profile == LL_PCODE_PROFILE_EQUALTRI )
				{
					selected_item = "prism";
				}
				else if ( linear_path && profile == LL_PCODE_PROFILE_RIGHTTRI )
				{
					selected_item = "prism";
				}
				else if (path == LL_PCODE_PATH_FLEXIBLE) // shouldn't happen
				{
					selected_item = "cylinder"; // reasonable default
				}
				else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_CIRCLE && scale_y > 0.75f)
				{
					selected_item = "sphere";
				}
				else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_CIRCLE && scale_y <= 0.75f)
				{
					selected_item = "torus";
				}
				else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_CIRCLE_HALF)
				{
					selected_item = "sphere";
				}
				else if ( path == LL_PCODE_PATH_CIRCLE2 && profile == LL_PCODE_PROFILE_CIRCLE )
				{
					// Spirals aren't supported.  Make it into a sphere.  JC
					selected_item = "sphere";
				}
				else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_EQUALTRI )
				{
					selected_item = "ring";
				}
				else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_SQUARE && scale_y <= 0.75f)
				{
					selected_item = "tube";
				}
				else
				{
					llinfos << "Unknown path " << (S32) path << " profile " << (S32) profile << " in getState" << llendl;
					selected_item = "box";
				}
				// Create an LLSD object that represents this prim. It will be injected in to the overall LLSD
				// tree structure
				LLXMLNode *prim_xml;
				LLPCode pcode = prim["pcode"].asInteger();
				// Sculpt
				if (prim.has("sculpt"))
					prim_xml = new LLXMLNode("sculpt", FALSE);
				else if (pcode == LL_PCODE_LEGACY_GRASS)
				{
					prim_xml = new LLXMLNode("grass", FALSE);
					LLXMLNodePtr shadow_xml = prim_xml->createChild("type", FALSE);
					shadow_xml->createChild("val", TRUE)->setValue(prim["state"]);
				}
				else if (pcode == LL_PCODE_LEGACY_TREE)
				{
					prim_xml = new LLXMLNode("tree", FALSE);
					LLXMLNodePtr shadow_xml = prim_xml->createChild("type", FALSE);
					shadow_xml->createChild("val", TRUE)->setValue(prim["state"]);
				}
				else
					prim_xml = new LLXMLNode(selected_item.c_str(), FALSE);
				
				//Properties
				
				LLSD * props=received_properties[prim["id"]];

				if(props!=NULL)
				{
					prim_xml->createChild("name", FALSE)->setStringValue(std::string((*props)["name"]));
					prim_xml->createChild("description", FALSE)->setStringValue(std::string((*props)["description"]));
					prim_xml->createChild("uuid",FALSE)->setValue(prim["id"].asString());
					
					//All done with properties?
					free(props);
					received_properties.erase(prim["id"]);
				}
				
				// Transforms		
				LLXMLNodePtr position_xml = prim_xml->createChild("position", FALSE);
				LLVector3 position;
				position.setVec(ll_vector3d_from_sd(prim["position"]));
				position_xml->createChild("x", TRUE)->setValue(llformat("%.5f", position.mV[VX]));
				position_xml->createChild("y", TRUE)->setValue(llformat("%.5f", position.mV[VY]));
				position_xml->createChild("z", TRUE)->setValue(llformat("%.5f", position.mV[VZ]));
				
				LLXMLNodePtr scale_xml = prim_xml->createChild("size", FALSE);
				LLVector3 scale = ll_vector3_from_sd(prim["scale"]);
				scale_xml->createChild("x", TRUE)->setValue(llformat("%.5f", scale.mV[VX]));
				scale_xml->createChild("y", TRUE)->setValue(llformat("%.5f", scale.mV[VY]));
				scale_xml->createChild("z", TRUE)->setValue(llformat("%.5f", scale.mV[VZ]));
				
				LLXMLNodePtr rotation_xml = prim_xml->createChild("rotation", FALSE);
				LLQuaternion rotation = ll_quaternion_from_sd(prim["rotation"]);
				rotation_xml->createChild("x", TRUE)->setValue(llformat("%.5f", rotation.mQ[VX]));
				rotation_xml->createChild("y", TRUE)->setValue(llformat("%.5f", rotation.mQ[VY]));
				rotation_xml->createChild("z", TRUE)->setValue(llformat("%.5f", rotation.mQ[VZ]));
				rotation_xml->createChild("w", TRUE)->setValue(llformat("%.5f", rotation.mQ[VW]));
				// Flags
				if(prim["phantom"].asBoolean())
				{
					LLXMLNodePtr shadow_xml = prim_xml->createChild("phantom", FALSE);
					shadow_xml->createChild("val", TRUE)->setValue("true");
				}
				if(prim["physical"].asBoolean())
				{
					LLXMLNodePtr shadow_xml = prim_xml->createChild("physical", FALSE);
					shadow_xml->createChild("val", TRUE)->setValue("true");
				}
				if (export_is_avatar)
				{
					LLXMLNodePtr attach_xml = prim_xml->createChild("attach_point", FALSE);
					attach_xml->createChild("val", TRUE)->setValue(prim["Attachment"]);

					LLXMLNodePtr attachp_xml = prim_xml->createChild("attach_position", FALSE);
					LLVector3 attachpos = ll_vector3_from_sd(prim["attachpos"]);
					attachp_xml->createChild("x", TRUE)->setValue(llformat("%.5f", attachpos.mV[VX]));
					attachp_xml->createChild("y", TRUE)->setValue(llformat("%.5f", attachpos.mV[VY]));
					attachp_xml->createChild("z", TRUE)->setValue(llformat("%.5f", attachpos.mV[VZ]));

					LLXMLNodePtr attachr_xml = prim_xml->createChild("attach_rotation", FALSE);
					LLQuaternion attachrot = ll_quaternion_from_sd(prim["attachrot"]);
					attachr_xml->createChild("x", TRUE)->setValue(llformat("%.5f", attachrot.mQ[VX]));
					attachr_xml->createChild("y", TRUE)->setValue(llformat("%.5f", attachrot.mQ[VY]));
					attachr_xml->createChild("z", TRUE)->setValue(llformat("%.5f", attachrot.mQ[VZ]));
					attachr_xml->createChild("w", TRUE)->setValue(llformat("%.5f", attachrot.mQ[VW]));
				}
				
				// Grab S path
				F32 begin_s	= volume_params.getBeginS();
				F32 end_s	= volume_params.getEndS();
				// Compute cut and advanced cut from S and T
				F32 begin_t = volume_params.getBeginT();
				F32 end_t	= volume_params.getEndT();
				// Hollowness
				F32 hollow = volume_params.getHollow();
				// Twist
				F32 twist		= volume_params.getTwist();
				F32 twist_begin = volume_params.getTwistBegin();
				if (path == LL_PCODE_PATH_LINE || path == LL_PCODE_PATH_FLEXIBLE)
				{
					twist		*= OBJECT_TWIST_LINEAR_MAX;
					twist_begin	*= OBJECT_TWIST_LINEAR_MAX;
				}
				else
				{
					twist		*= OBJECT_TWIST_MAX;
					twist_begin	*= OBJECT_TWIST_MAX;
				}
				// Cut interpretation varies based on base object type
				F32 cut_begin, cut_end, adv_cut_begin, adv_cut_end;
				if ( selected_item == "sphere" || selected_item == "torus" || 
					 selected_item == "tube"   || selected_item == "ring" )
				{
					cut_begin		= begin_t;
					cut_end			= end_t;
					adv_cut_begin	= begin_s;
					adv_cut_end		= end_s;
				}
				else
				{
					cut_begin       = begin_s;
					cut_end         = end_s;
					adv_cut_begin   = begin_t;
					adv_cut_end     = end_t;
				}
				if (selected_item != "sphere")
				{		
					// Shear
					//<top_shear x="0" y="0" />
					F32 shear_x = volume_params.getShearX();
					F32 shear_y = volume_params.getShearY();
					LLXMLNodePtr shear_xml = prim_xml->createChild("top_shear", FALSE);
					shear_xml->createChild("x", TRUE)->setValue(llformat("%.5f", shear_x));
					shear_xml->createChild("y", TRUE)->setValue(llformat("%.5f", shear_y));
				}
				else
				{	
					// Dimple
					//<dimple begin="0.0" end="1.0" />
					LLXMLNodePtr shear_xml = prim_xml->createChild("dimple", FALSE);
					shear_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", adv_cut_begin));
					shear_xml->createChild("end", TRUE)->setValue(llformat("%.5f", adv_cut_end));
				}

				if (selected_item == "box" || selected_item == "cylinder" || selected_item == "prism")
				{
					// Taper
					//<taper x="0" y="0" />
					F32 taper_x = 1.f - volume_params.getRatioX();
					F32 taper_y = 1.f - volume_params.getRatioY();
					LLXMLNodePtr taper_xml = prim_xml->createChild("taper", FALSE);
					taper_xml->createChild("x", TRUE)->setValue(llformat("%.5f", taper_x));
					taper_xml->createChild("y", TRUE)->setValue(llformat("%.5f", taper_y));
					// Slice (Hacked Dimple)
					//<slice begin="0.0" end="1.0" />
					LLXMLNodePtr shear_xml = prim_xml->createChild("slice", FALSE);
					shear_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", adv_cut_begin));
					shear_xml->createChild("end", TRUE)->setValue(llformat("%.5f", adv_cut_end));

				}
				else if (selected_item == "torus" || selected_item == "tube" || selected_item == "ring")
				{
					// Taper
					//<taper x="0" y="0" />
					F32 taper_x	= volume_params.getTaperX();
					F32 taper_y = volume_params.getTaperY();
					LLXMLNodePtr taper_xml = prim_xml->createChild("taper", FALSE);
					taper_xml->createChild("x", TRUE)->setValue(llformat("%.5f", taper_x));
					taper_xml->createChild("y", TRUE)->setValue(llformat("%.5f", taper_y));
					//Hole Size
					//<hole_size x="0.2" y="0.35" />
					F32 hole_size_x = volume_params.getRatioX();
					F32 hole_size_y = volume_params.getRatioY();
					LLXMLNodePtr hole_size_xml = prim_xml->createChild("hole_size", FALSE);
					hole_size_xml->createChild("x", TRUE)->setValue(llformat("%.5f", hole_size_x));
					hole_size_xml->createChild("y", TRUE)->setValue(llformat("%.5f", hole_size_y));
					//Advanced cut
					//<profile_cut begin="0" end="1" />
					LLXMLNodePtr profile_cut_xml = prim_xml->createChild("profile_cut", FALSE);
					profile_cut_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", adv_cut_begin));
					profile_cut_xml->createChild("end", TRUE)->setValue(llformat("%.5f", adv_cut_end));
					//Skew
					//<skew val="0.0" />
					F32 skew = volume_params.getSkew();
					LLXMLNodePtr skew_xml = prim_xml->createChild("skew", FALSE);
					skew_xml->createChild("val", TRUE)->setValue(llformat("%.5f", skew));
					//Radius offset
					//<radius_offset val="0.0" />
					F32 radius_offset = volume_params.getRadiusOffset();
					LLXMLNodePtr radius_offset_xml = prim_xml->createChild("radius_offset", FALSE);
					radius_offset_xml->createChild("val", TRUE)->setValue(llformat("%.5f", radius_offset));
					// Revolutions
					//<revolutions val="1.0" />
					F32 revolutions = volume_params.getRevolutions();
					LLXMLNodePtr revolutions_xml = prim_xml->createChild("revolutions", FALSE);
					revolutions_xml->createChild("val", TRUE)->setValue(llformat("%.5f", revolutions));
				}
				//<path_cut begin="0" end="1" />
				LLXMLNodePtr path_cut_xml = prim_xml->createChild("path_cut", FALSE);
				path_cut_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", cut_begin));
				path_cut_xml->createChild("end", TRUE)->setValue(llformat("%.5f", cut_end));
				//<twist begin="0" end="0" />
				LLXMLNodePtr twist_xml = prim_xml->createChild("twist", FALSE);
				twist_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", twist_begin));
				twist_xml->createChild("end", TRUE)->setValue(llformat("%.5f", twist));
				// All hollow objects allow a shape to be selected.
				if (hollow > 0.f)
				{
					const char	*selected_hole	= "1";
					switch (hole)
					{
					case LL_PCODE_HOLE_CIRCLE:
						selected_hole = "3";
						break;
					case LL_PCODE_HOLE_SQUARE:
						selected_hole = "2";
						break;
					case LL_PCODE_HOLE_TRIANGLE:
						selected_hole = "4";
						break;
					case LL_PCODE_HOLE_SAME:
					default:
						selected_hole = "1";
						break;
					}
					//<hollow amount="0" shape="1" />
					LLXMLNodePtr hollow_xml = prim_xml->createChild("hollow", FALSE);
					hollow_xml->createChild("amount", TRUE)->setValue(llformat("%.5f", hollow * 100.0));
					hollow_xml->createChild("shape", TRUE)->setValue(llformat("%s", selected_hole));
				}
				// Extra params
				// Flexible
				if(prim.has("flexible"))
				{
					LLFlexibleObjectData attributes;
					attributes.fromLLSD(prim["flexible"]);
					//<flexible>
					LLXMLNodePtr flex_xml = prim_xml->createChild("flexible", FALSE);
					//<softness val="2.0">
					LLXMLNodePtr softness_xml = flex_xml->createChild("softness", FALSE);
					softness_xml->createChild("val", TRUE)->setValue(llformat("%.5f", (F32)attributes.getSimulateLOD()));
					//<gravity val="0.3">
					LLXMLNodePtr gravity_xml = flex_xml->createChild("gravity", FALSE);
					gravity_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getGravity()));
					//<drag val="2.0">
					LLXMLNodePtr drag_xml = flex_xml->createChild("drag", FALSE);
					drag_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getAirFriction()));
					//<wind val="0.0">
					LLXMLNodePtr wind_xml = flex_xml->createChild("wind", FALSE);
					wind_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getWindSensitivity()));
					//<tension val="1.0">
					LLXMLNodePtr tension_xml = flex_xml->createChild("tension", FALSE);
					tension_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getTension()));
					//<force x="0.0" y="0.0" z="0.0" />
					LLXMLNodePtr force_xml = flex_xml->createChild("force", FALSE);
					force_xml->createChild("x", TRUE)->setValue(llformat("%.5f", attributes.getUserForce().mV[VX]));
					force_xml->createChild("y", TRUE)->setValue(llformat("%.5f", attributes.getUserForce().mV[VY]));
					force_xml->createChild("z", TRUE)->setValue(llformat("%.5f", attributes.getUserForce().mV[VZ]));
				}
				
				// Light
				if (prim.has("light"))
				{
					LLLightParams light;
					light.fromLLSD(prim["light"]);
					//<light>
					LLXMLNodePtr light_xml = prim_xml->createChild("light", FALSE);
					//<color r="255" g="255" b="255" />
					LLXMLNodePtr color_xml = light_xml->createChild("color", FALSE);
					LLColor4 color = light.getColor();
					color_xml->createChild("r", TRUE)->setValue(llformat("%u", (U32)(color.mV[VRED] * 255)));
					color_xml->createChild("g", TRUE)->setValue(llformat("%u", (U32)(color.mV[VGREEN] * 255)));
					color_xml->createChild("b", TRUE)->setValue(llformat("%u", (U32)(color.mV[VBLUE] * 255)));
					//<intensity val="1.0" />
					LLXMLNodePtr intensity_xml = light_xml->createChild("intensity", FALSE);
					intensity_xml->createChild("val", TRUE)->setValue(llformat("%.5f", color.mV[VALPHA]));
					//<radius val="10.0" />
					LLXMLNodePtr radius_xml = light_xml->createChild("radius", FALSE);
					radius_xml->createChild("val", TRUE)->setValue(llformat("%.5f", light.getRadius()));
					//<falloff val="0.75" />
					LLXMLNodePtr falloff_xml = light_xml->createChild("falloff", FALSE);
					falloff_xml->createChild("val", TRUE)->setValue(llformat("%.5f", light.getFalloff()));
				}
				// Sculpt
				if (prim.has("sculpt"))
				{
					LLSculptParams sculpt;
					sculpt.fromLLSD(prim["sculpt"]);
					
					//<topology val="4" />
					LLXMLNodePtr topology_xml = prim_xml->createChild("topology", FALSE);
					topology_xml->createChild("val", TRUE)->setValue(llformat("%u", sculpt.getSculptType()));
					
					//<sculptmap_uuid>1e366544-c287-4fff-ba3e-5fafdba10272</sculptmap_uuid>
					//<sculptmap_file>apple_map.tga</sculptmap_file>
					//FIXME png/tga/j2c selection itt.
					std::string sculpttexture;
					sculpt.getSculptTexture().toString(sculpttexture);
					prim_xml->createChild("sculptmap_file", FALSE)->setValue(sculpttexture+".tga");
					prim_xml->createChild("sculptmap_uuid", FALSE)->setValue(sculpttexture);
				}
				//<texture>
				LLXMLNodePtr texture_xml = prim_xml->createChild("texture", FALSE);
				// Textures
				LLSD te_llsd;
				LLSD tes = prim["textures"];
				LLPrimitive object;
				object.setNumTEs(U8(tes.size()));
				
				for (int i = 0; i < tes.size(); i++)
				{
					LLTextureEntry tex;
					tex.fromLLSD(tes[i]);
					object.setTE(U8(i), tex);
				}
			
				//U8 te_count = object->getNumTEs();
				//for (U8 i = 0; i < te_count; i++)
				//{
				//for each texture
				for (int i = 0; i < tes.size(); i++)
				{
					LLTextureEntry tex;
					tex.fromLLSD(tes[i]);
					//bool alreadyseen=false;
					//te_llsd.append(object->getTE(i)->asLLSD());
					std::list<LLUUID>::iterator iter;
					
					/* this loop keeps track of seen textures, replace with other.
					for(iter = textures.begin(); iter != textures.end() ; iter++)
					{
						if( (*iter)==object->getTE(i)->getID())
							alreadyseen=true;
					}
					*/
					//<face id=0>
					LLXMLNodePtr face_xml = texture_xml->createChild("face", FALSE);
					//This may be a hack, but it's ok since we're not using id in this code. We set id differently because for whatever reason
					//llxmlnode filters a few parameters including ID. -Patrick Sapinski (Friday, September 25, 2009)
					face_xml->mID = llformat("%d", i);
					//<tile u="-1" v="1" />
					//object->getTE(face)->mScaleS
					//object->getTE(face)->mScaleT
					LLXMLNodePtr tile_xml = face_xml->createChild("tile", FALSE);
					tile_xml->createChild("u", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mScaleS));
					tile_xml->createChild("v", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mScaleT));
					//<offset u="0" v="0" />
					//object->getTE(face)->mOffsetS
					//object->getTE(face)->mOffsetT
					LLXMLNodePtr offset_xml = face_xml->createChild("offset", FALSE);
					offset_xml->createChild("u", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mOffsetS));
					offset_xml->createChild("v", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mOffsetT));
					//<rotation w="0" />
					//object->getTE(face)->mRotation
					LLXMLNodePtr rotation_xml = face_xml->createChild("rotation", FALSE);
					rotation_xml->createChild("w", TRUE)->setValue(llformat("%.5f", (object.getTE(i)->mRotation * RAD_TO_DEG)));
					//<image_file><![CDATA[76a0319a-e963-44b0-9f25-127815226d71.tga]]></image_file>
					//<image_uuid>76a0319a-e963-44b0-9f25-127815226d71</image_uuid>
					LLUUID texture = object.getTE(i)->getID();
					std::string uuid_string;
					object.getTE(i)->getID().toString(uuid_string);
					
					face_xml->createChild("image_file", FALSE)->setStringValue(uuid_string);
					face_xml->createChild("image_uuid", FALSE)->setValue(uuid_string);
					//<color r="255" g="255" b="255" />
					LLXMLNodePtr color_xml = face_xml->createChild("color", FALSE);
					LLColor4 color = object.getTE(i)->getColor();
					color_xml->createChild("r", TRUE)->setValue(llformat("%u", (int)(color.mV[VRED] * 255.f)));
					color_xml->createChild("g", TRUE)->setValue(llformat("%u", (int)(color.mV[VGREEN] * 255.f)));
					color_xml->createChild("b", TRUE)->setValue(llformat("%u", (int)(color.mV[VBLUE] * 255.f)));
					//<transparency val="0" />
					LLXMLNodePtr transparency_xml = face_xml->createChild("transparency", FALSE);
					transparency_xml->createChild("val", TRUE)->setValue(llformat("%u", (int)((1.f - color.mV[VALPHA]) * 100.f)));
					//<glow val="0" />
					//object->getTE(face)->getGlow()
					LLXMLNodePtr glow_xml = face_xml->createChild("glow", FALSE);
					glow_xml->createChild("val", TRUE)->setValue(llformat("%.5f", object.getTE(i)->getGlow()));
					//HACK! primcomposer chokes if we have fullbright but don't specify shine+bump.
					//<fullbright val="false" />
					//<shine val="0" />
					//<bump val="0" />
					if(object.getTE(i)->getFullbright() || object.getTE(i)->getShiny() || object.getTE(i)->getBumpmap())
					{
						std::string temp = "false";
						if(object.getTE(i)->getFullbright())
							temp = "true";
						LLXMLNodePtr fullbright_xml = face_xml->createChild("fullbright", FALSE);
						fullbright_xml->createChild("val", TRUE)->setValue(temp);
						LLXMLNodePtr shine_xml = face_xml->createChild("shine", FALSE);
						shine_xml->createChild("val", TRUE)->setValue(llformat("%u",object.getTE(i)->getShiny()));
						LLXMLNodePtr bumpmap_xml = face_xml->createChild("bump", FALSE);
						bumpmap_xml->createChild("val", TRUE)->setValue(llformat("%u",object.getTE(i)->getBumpmap()));
					}
						
					//<mapping val="0" />
					face_xml->createChild("mapping", FALSE)->createChild("val", TRUE)->setValue(llformat("%d", object.getTE(i)->getTexGen()));
				} // end for each texture
				//<inventory>

				
				LLXMLNodePtr inventory_xml = prim_xml->createChild("inventory", FALSE);
				//LLSD inventory = prim["inventory"];
				LLSD * inventory = received_inventory[prim["id"]];

				if(inventory !=NULL)
				{
					if(prim["inventoryReceived"].asBoolean())
					{
						//flag that we have already received inventory contents
						inventory_xml->createChild("received", FALSE)->setValue("true");
					}

					//for each inventory item
					for (LLSD::array_iterator inv = (*inventory).beginArray(); inv != (*inventory).endArray(); ++inv)
					{
						LLSD item = (*inv);
					
						//<item>
						LLXMLNodePtr field_xml = inventory_xml->createChild("item", FALSE);
						   //<description>2008-01-29 05:01:19 note card</description>
						field_xml->createChild("description", FALSE)->setValue(item["desc"].asString());
						   //<item_id>673b00e8-990f-3078-9156-c7f7b4a5f86c</item_id>
						field_xml->createChild("item_id", FALSE)->setValue(item["item_id"].asString());
						   //<name>blah blah</name>
						field_xml->createChild("name", FALSE)->setValue(item["name"].asString());
						   //<type>notecard</type>
						field_xml->createChild("type", FALSE)->setValue(item["type"].asString());
					} // end for each inventory item
					//add this prim to the linkset.

					delete(inventory);
					received_inventory.erase(LLUUID(prim["id"].asString()));

				}
				linkset_xml->addChild(prim_xml);
			} //end for each object
			//add this linkset to the group.
			group_xml->addChild(linkset_xml);
			delete plsd;
		} //end for each linkset.

		// Create a file stream and write to it
		llofstream out(destination,ios_base::out);
		if (!out.good())
		{
			llwarns << "Unable to open for output." << llendl;
		}
		else
		{
			out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
			project_xml->addChild(group_xml);
			project_xml->writeToOstream(out);
			out.close();
		}

		/* this code gzips the archive, we want to zip it though!
		std::string gzip_filename(destination);
		gzip_filename.append(".gz");
		if(gzip_file(destination, gzip_filename))
		{
			lldebugs << "Successfully compressed " << destination << llendl;
			//LLFile::remove(inventory_filename);
		}
		else
		{
			llwarns << "Unable to compress " << destination << llendl;
		}
		*/



/*	we can still output an LLSD with this but it's no longer compatible with other exporters because 
	we switched to region relative positions. useful for testing. */

//	LLSD file;
//	LLSD header;
//	header["Version"] = 2;
//	file["Header"] = header;
	//std::vector<std::string> uris;
	//	LLViewerLogin* vl = LLViewerLogin::getInstance();
	//	std::string grid_uri = vl->getGridLabel(); //RC FIXME
	//LLStringUtil::toLower(uris[0]);
//	file["Grid"] = grid_uri;
//	file["Objects"] = data;

	// Create a file stream and write to it
//	llofstream export_file(destination + ".llsd",std::ios_base::app | std::ios_base::out);
//	LLSDSerialize::toPrettyXML(file, export_file);
	// Open the file save dialog
//	export_file.close();

	processed_prims.clear();
	received_inventory.clear();

	JCExportTracker::cleanup();

	mExportFloater->childSetEnabled("export",true);
	mStatus = IDLE;
	mExportFloater->refresh();
}

void JCExportTracker::processObjectProperties(LLMessageSystem* msg, void** user_data)
{
	if(sInstance)
		JCExportTracker::getInstance()->actuallyprocessObjectProperties(msg);
}
void JCExportTracker::actuallyprocessObjectProperties(LLMessageSystem* msg)
{
	S32 i;
	S32 count = msg->getNumberOfBlocksFast(_PREHASH_ObjectData);
	for (i = 0; i < count; i++)
	{
		LLUUID id;
		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_ObjectID, id, i);

		//Remove this request from the list of requests
		std::list<PropertiesRequest_t*>::iterator iter;
		for (iter=requested_properties.begin(); iter != requested_properties.end(); iter++)
		{
			PropertiesRequest_t * req;
			req=(*iter);
			if(id==req->target_prim)
			{
				free(req);
				requested_properties.erase(iter);
				if(mStatus == EXPORTING)
					mPropertiesReceived++;
				break;
			}
		}

		if ((iter == requested_properties.end())&&(requested_properties.size() > 1))
		{
			llinfos << "Could not find target prim, skipping." << llendl;
			return;
		}
			
		LLUUID creator_id;
		LLUUID owner_id;
		LLUUID group_id;
		LLUUID last_owner_id;
		U64 creation_date;
		LLUUID extra_id;
		U32 base_mask, owner_mask, group_mask, everyone_mask, next_owner_mask;
		LLSaleInfo sale_info;
		LLCategory category;
		LLAggregatePermissions ag_perms;
		LLAggregatePermissions ag_texture_perms;
		LLAggregatePermissions ag_texture_perms_owner;
								
		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_CreatorID, creator_id, i);
		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_OwnerID, owner_id, i);
		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_GroupID, group_id, i);
		msg->getU64Fast(_PREHASH_ObjectData, _PREHASH_CreationDate, creation_date, i);
		msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_BaseMask, base_mask, i);
		msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_OwnerMask, owner_mask, i);
		msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_GroupMask, group_mask, i);
		msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_EveryoneMask, everyone_mask, i);
		msg->getU32Fast(_PREHASH_ObjectData, _PREHASH_NextOwnerMask, next_owner_mask, i);
		sale_info.unpackMultiMessage(msg, _PREHASH_ObjectData, i);

		ag_perms.unpackMessage(msg, _PREHASH_ObjectData, _PREHASH_AggregatePerms, i);
		ag_texture_perms.unpackMessage(msg, _PREHASH_ObjectData, _PREHASH_AggregatePermTextures, i);
		ag_texture_perms_owner.unpackMessage(msg, _PREHASH_ObjectData, _PREHASH_AggregatePermTexturesOwner, i);
		category.unpackMultiMessage(msg, _PREHASH_ObjectData, i);

		S16 inv_serial = 0;
		msg->getS16Fast(_PREHASH_ObjectData, _PREHASH_InventorySerial, inv_serial, i);

		LLUUID item_id;
		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_ItemID, item_id, i);
		LLUUID folder_id;
		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_FolderID, folder_id, i);
		LLUUID from_task_id;
		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_FromTaskID, from_task_id, i);

		msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_LastOwnerID, last_owner_id, i);

		std::string name;
		msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Name, name, i);
		std::string desc;
		msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Description, desc, i);

		std::string touch_name;
		msg->getStringFast(_PREHASH_ObjectData, _PREHASH_TouchName, touch_name, i);
		std::string sit_name;
		msg->getStringFast(_PREHASH_ObjectData, _PREHASH_SitName, sit_name, i);

		//unpack TE IDs
		std::vector<LLUUID> texture_ids;
		S32 size = msg->getSizeFast(_PREHASH_ObjectData, i, _PREHASH_TextureID);
		if (size > 0)
		{
			S8 packed_buffer[SELECT_MAX_TES * UUID_BYTES];
			msg->getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_TextureID, packed_buffer, 0, i, SELECT_MAX_TES * UUID_BYTES);

			for (S32 buf_offset = 0; buf_offset < size; buf_offset += UUID_BYTES)
			{
				LLUUID tid;
				memcpy(tid.mData, packed_buffer + buf_offset, UUID_BYTES);		/* Flawfinder: ignore */
				texture_ids.push_back(tid);
			}
		}

		LLSD * props=new LLSD();

		(*props)["creator_id"] = creator_id.asString();
		(*props)["owner_id"] = owner_id.asString();
		(*props)["group_id"] = group_id.asString();
		(*props)["last_owner_id"] = last_owner_id.asString();
		(*props)["creation_date"] = llformat("%d",creation_date);
		(*props)["extra_id"] = extra_id.asString();
		(*props)["base_mask"] = llformat("%d",base_mask);
		(*props)["owner_mask"] = llformat("%d",owner_mask);
		(*props)["group_mask"] = llformat("%d",group_mask);
		(*props)["everyone_mask"] = llformat("%d",everyone_mask);
		(*props)["next_owner_mask"] = llformat("%d",next_owner_mask);
		(*props)["sale_info"] = sale_info.asLLSD();

		(*props)["inv_serial"] = (S32)inv_serial;
		(*props)["item_id"] = item_id.asString();
		(*props)["folder_id"] = folder_id.asString();
		(*props)["from_task_id"] = from_task_id.asString();
		(*props)["name"] = name;
		(*props)["description"] = desc;
		(*props)["touch_name"] = touch_name;
		(*props)["sit_name"] = sit_name;

		received_properties[id]=props;

		LLViewerObject *obj = gObjectList.findObject(id);
		if(obj)
		{
			obj->mPropertiesRecieved = true;
			llinfos << "Got properties for " << obj->getID() << " (" << name << ")." << llendl;
		}
		else
		{
			llwarns << "Failed to find object " << id << " and mark as properties recieved, wtf?" << llendl;
		}
	}
}

BOOL couldDL(LLAssetType::EType type)
{
	switch(type)
	{//things we could plausibly DL anyway atm
	case LLAssetType::AT_TEXTURE:
	case LLAssetType::AT_SCRIPT:
	case LLAssetType::AT_SOUND:
	case LLAssetType::AT_ANIMATION:
	case LLAssetType::AT_CLOTHING:
	case LLAssetType::AT_NOTECARD:
	case LLAssetType::AT_LSL_TEXT:
	case LLAssetType::AT_TEXTURE_TGA:
	case LLAssetType::AT_BODYPART:
	case LLAssetType::AT_GESTURE:
		return TRUE;
	default:
		break;
	}
	return FALSE;
}
LLUUID JCExportTracker::getUUIDForAsset(LLInventoryItem* item)
{
	LLUUID temp = item->getAssetUUID();
	if(temp.isNull())
		temp = item->getUUID();
	return temp;
}
BOOL JCExportTracker::exportAllowed(const LLPermissions *perm)
{
#if FOLLOW_PERMS == 0
	return TRUE;
#else

	bool export_ok = true;

	if(LLGridManager::getInstance()->isInSLMain() || LLGridManager::getInstance()->isInSLBeta())
		if(perm->getCreator() != gAgent.getID()) export_ok = false;

	if(perm->getOwner() != gAgent.getID()) export_ok = false;
	if((perm->getMaskOwner() & PERM_ITEM_UNRESTRICTED) != PERM_ITEM_UNRESTRICTED) export_ok = false;

	return export_ok;
#endif
}

BOOL JCExportTracker::isSurrogateDeleteable(LLViewerObject* obj)
{
	BOOL deleteable = TRUE;

	if(obj->mInventoryRecieved)
	{
		LLViewerObject::child_list_t child_list;
		child_list = obj->getChildren();

		llassert(!child_list.empty());

		for (LLViewerObject::child_list_t::const_iterator i = child_list.begin(); i != child_list.end(); i++)
		{
			if(!(*i)->mInventoryRecieved)
			{
				deleteable = FALSE;
				break;
			}
		}
	}
	else
		deleteable = FALSE;

	return deleteable;
}

void JCExportTracker::inventoryChanged(LLViewerObject* obj,
								 LLInventoryObject::object_list_t* inv,
								 S32 serial_num,
								 void* user_data)
{
	if(!export_inventory)
	{
		obj->removeInventoryListener(sInstance);
		return;
	}
	if(mStatus == IDLE)
	{
		obj->removeInventoryListener(sInstance);
		return;
	}

	if(requested_inventory.empty())
		return;

	std::list<InventoryRequest_t*>::iterator iter=requested_inventory.begin();
	for(;iter!=requested_inventory.end();iter++)
	{
		if((*iter)->object->getID()==obj->getID())
		{
			received_inventory[(*iter)->real_object->getID()] = checkInventoryContents((*iter), inv);

			//cmdline_printchat(llformat("%d inv queries left",requested_inventory.size()));

			obj->removeInventoryListener(sInstance);
			obj->mInventoryRecieved=true;
			(*iter)->real_object->mInventoryRecieved=true;

			LLViewerObject* linkset_root;

			if(obj->isRoot())
				linkset_root = obj;
			else
				linkset_root = obj->getSubParent();

			//are we working with a surrogate? check if it can be removed
			if((*iter)->real_object != obj && isSurrogateDeleteable(linkset_root))
			{
				std::list<LLViewerObject *>::iterator surr_iter=surrogate_roots.begin();
				for(;surr_iter!=surrogate_roots.end();surr_iter++)
				{
					if((*surr_iter)->getID()==obj->getID())
					{
						surrogate_roots.erase(surr_iter);
						break;
					}
				}
				removeObject(linkset_root);
			}

			requested_inventory.erase(iter);
			mInventoriesReceived++;
			break;
		}
	}
}

LLSD* JCExportTracker::checkInventoryContents(InventoryRequest_t* original_request, LLInventoryObject::object_list_t* inv)
{
	LLSD * inventory = new LLSD();

	const S32 mAT_ROOT_CATEGORY = 9;

	LLInventoryObject::object_list_t::const_iterator it = inv->begin();
	LLInventoryObject::object_list_t::const_iterator end = inv->end();

	LLViewerObject* obj = original_request->object;
	LLViewerObject* source_obj = original_request->real_object;

	U32 num = 0;
	for( ;	it != end;	++it)
	{
		LLInventoryObject* asset = it->get();

		// Skip folders, so we know we have inventory items only
		if (asset->getType() == LLAssetType::AT_CATEGORY) {
			//cmdline_printchat("skipping AT_CATEGORY");
			continue;
		}

		// Skip root folders, so we know we have inventory items only
		if (asset->getType() == mAT_ROOT_CATEGORY) { //maybe check folder iso type?
			cmdline_printchat("skipping AT_ROOT_CATEGORY");
			continue;
		}

		// Skip the mysterious blank InventoryObject 
		if (asset->getType() == LLAssetType::AT_NONE) {
			//cmdline_printchat("skipping AT_NONE");
			continue;
		}

		if(asset)
		{
			LLInventoryItem* item = (LLInventoryItem*)asset;
			LLViewerInventoryItem* new_item = (LLViewerInventoryItem*)item;


			BOOL is_complete = new_item->isFinished();
			if (!is_complete)
				cmdline_printchat("not complete!");

			const LLPermissions perm = new_item->getPermissions();
			if(exportAllowed(&perm))
			{
				if(couldDL(asset->getType()))
				{
					LLUUID uuid = getUUIDForAsset(item);
					LLSD inv_item;
					inv_item["name"] = asset->getName();
					inv_item["type"] = LLAssetType::lookup(asset->getType());
					//cmdline_printchat("requesting asset "+asset->getName());
					inv_item["desc"] = item->getDescription();
					inv_item["item_id"] = uuid;
					if(!LLFile::isdir(asset_dir+gDirUtilp->getDirDelimiter()+"inventory"))
					{
						LLFile::mkdir(asset_dir+gDirUtilp->getDirDelimiter()+"inventory");
					}

					JCExportTracker::download(asset, obj, asset_dir+gDirUtilp->getDirDelimiter()+"inventory", uuid.asString());//loltest
					(*inventory)[num++] = inv_item;
					mTotalAssets++;
				}
				//recursive objects will be handled here -- Cryo
				//TODO: Add check for already done objects incase something is using the same asset
				/*else if(asset->getType() == LLAssetType::AT_OBJECT)
				{
				}*/
				else if(asset->getType() != mAT_ROOT_CATEGORY) //we don't care about root folders at all!
					error(asset->getName(), source_obj->getLocalID(), source_obj->getPositionRegion(), llformat("%s%s", "Unsupported asset type: ", LLAssetType::lookup(asset->getType())));
			}
			else
				error(asset->getName(), source_obj->getLocalID(), source_obj->getPositionRegion(), "Insufficient permissions");
		}
	}

	return inventory;
}

void JCAssetExportCallback(LLVFS *vfs, const LLUUID& uuid, LLAssetType::EType type, void *userdata, S32 result, LLExtStat extstat)
{
	JCAssetInfo* info = (JCAssetInfo*)userdata;
	ExportTrackerFloater* export_floater = JCExportTracker::getInstance()->mExportFloater;
	LLScrollListCtrl* mResultList = export_floater->getChild<LLScrollListCtrl>("inventory_result_list");

	if(result == LL_ERR_NOERR)
	{
		//cmdline_printchat("Saved file "+info->path+" ("+info->name+")");
		S32 size = vfs->getSize(uuid, type);
		U8* buffer = new U8[size];
		vfs->getData(uuid, type, buffer, 0, size);

		if(type == LLAssetType::AT_NOTECARD)
		{
			static LLViewerTextEditor* edit = NULL;
			if(!edit) {
				LLViewerTextEditor::Params p = LLViewerTextEditor::Params();
				p.max_text_length(S32_MAX);
				edit = LLUICtrlFactory::create<LLViewerTextEditor>(p);
			}
			
			if(edit->importBuffer((char*)buffer, (S32)size))
			{
				//cmdline_printchat("Decoded notecard");
				std::string card = edit->getText();
				//delete edit;
				edit->die();
				delete buffer;
				buffer = new U8[card.size()];
				size = card.size();
				strcpy((char*)buffer,card.c_str());
			}else cmdline_printchat("Failed to decode notecard");
		}
		LLAPRFile infile;
		infile.open(info->path.c_str(), LL_APR_WB, TRUE);
		apr_file_t *fp = infile.getFileHandle();
		if(fp)infile.write(buffer, size);
		infile.close();

		JCExportTracker::getInstance()->mAssetsExported++;

		if (export_floater)
		{
			//mark as downloaded in inventory list
			if (mResultList->getItemIndex(info->assetid) >= 0)
			{
				export_floater->refresh();
				LLScrollListItem* invitemp = mResultList->getItem(info->assetid);
				invitemp->getColumn(4)->setValue("lag_status_good.tga");

				//LLSD temp = mResultList->getColumn(mResultList->getItemIndex(info->assetid))->getValue();
				//mResultList->deleteSingleItem(mResultList->getItemIndex(info->assetid));
				//mResultList->addElement(element, ADD_BOTTOM);

			}
			else 
				cmdline_printchat("received unrequested asset " +info->assetid.asString()+","+uuid.asString());

			//delete buffer;
		}
	} 
	else {
		//cmdline_printchat("Failed to save file "+info->path+" ("+info->name+") : "+std::string(LLAssetStorage::getErrorString(result)));
		LLScrollListItem* invitemp = mResultList->getItem(info->assetid);
		invitemp->getColumn(4)->setValue("ptt_lock_on.tga");
	}

	delete info;
}
void JCExportTracker::download(LLInventoryObject* item, LLViewerObject* container, std::string root, std::string iname)
{
		//add to floater list
		LLSD element;
		element["id"] = item->getUUID(); //object->getLocalID();

		element["columns"][0]["column"] = "Name";
		element["columns"][0]["type"] = "text";
		element["columns"][0]["value"] = item->getName();

		element["columns"][1]["column"] = "UUID";
		element["columns"][1]["type"] = "text";
		element["columns"][1]["value"] = getUUIDForAsset((LLInventoryItem*)item);

		element["columns"][2]["column"] = "Object ID";
		element["columns"][2]["type"] = "text";
		element["columns"][2]["value"] = llformat("%u",container->getLocalID());

		LLVector3 object_pos = container->getPositionRegion();
		std::stringstream sstr;
		sstr <<llformat("%.1f", object_pos.mV[VX]);
		sstr <<","<<llformat("%.1f", object_pos.mV[VY]);
		sstr <<","<<llformat("%.1f", object_pos.mV[VZ]);

		element["columns"][3]["column"] = "Position";
		element["columns"][3]["type"] = "text";
		element["columns"][3]["value"] = sstr.str();

		//element["columns"][4]["column"] = "Retries";
		//element["columns"][4]["type"] = "text";
		//element["columns"][4]["value"] = "0";

		element["columns"][4]["column"] = "icon_rec";
		element["columns"][4]["type"] = "icon";
		std::string filename = root + gDirUtilp->getDirDelimiter() + iname + "." + LLAssetType::lookup(item->getType());
		element["columns"][4]["value"] = "lag_status_warning.tga"; //pending
		//add it now
		LLScrollListCtrl* mResultList;
		mResultList = mExportFloater->getChild<LLScrollListCtrl>("inventory_result_list");
		mResultList->addElement(element, ADD_BOTTOM);
		if(LLFile::isfile(filename))
		{
			//cmdline_printchat("Duplicated inv item: " + iname); //test
			LLScrollListItem* invitemp = mResultList->getItem(item->getUUID());
			invitemp->getColumn(4)->setValue("icn_play.tga");
		}
		//time to download
		else if(!JCExportTracker::mirror(item, container, root, iname))//if it successeds it will be green
		{
			LLScrollListItem* invitemp = mResultList->getItem(item->getUUID());
			invitemp->getColumn(4)->setValue("ptt_lock_on.tga");//failed (possible because of permissions)
		}

}
BOOL JCExportTracker::mirror(LLInventoryObject* item, LLViewerObject* container, std::string root, std::string iname)
{
	if(item)
	{
		////cmdline_printchat("item");
		//LLUUID asset_id = item->getAssetUUID();
		//if(asset_id.notNull())
		LLPermissions perm(((LLInventoryItem*)item)->getPermissions());
		if(JCExportTracker::getInstance()->exportAllowed(&perm))
		{
			////cmdline_printchat("asset_id.notNull()");
			LLDynamicArray<std::string> tree;
			LLViewerInventoryCategory* cat = gInventory.getCategory(item->getParentUUID());
			while(cat)
			{
				std::string folder = cat->getName();
				folder = curl_escape(folder.c_str(), folder.size());
				tree.insert(tree.begin(),folder);
				cat = gInventory.getCategory(cat->getParentUUID());
			}
			if(container)
			{
				//tree.insert(tree.begin(),objectname i guess fuck);
				//wat
			}
			if(root == "")root = gSavedSettings.getString("InvMirrorLocation");
			if(!LLFile::isdir(root))
			{
				cmdline_printchat("Error: mirror root \""+root+"\" is nonexistant");
				return FALSE;
			}
			std::string path = gDirUtilp->getDirDelimiter();
			root = root + path;
			for (LLDynamicArray<std::string>::iterator it = tree.begin();
			it != tree.end();
			++it)
			{
				std::string folder = *it;
				root = root + folder;
				if(!LLFile::isdir(root))
				{
					LLFile::mkdir(root);
				}
				root = root + path;
				//cmdline_printchat(root);
			}
			if(iname == "")
			{
				iname = item->getName();
				iname = curl_escape(iname.c_str(), iname.size());
			}
			root = root + iname + "." + LLAssetType::lookup(item->getType());
			//cmdline_printchat(root);

			JCAssetInfo* info = new JCAssetInfo;
			info->path = root;
			info->name = item->getName();
			info->assetid = item->getUUID();

			LLInventoryItem* inv_item = (LLInventoryItem*)item; //info->assetid has the itemid... LAME

			switch(item->getType())
			{
			case LLAssetType::AT_TEXTURE:
			case LLAssetType::AT_NOTECARD:
			case LLAssetType::AT_SCRIPT:
			case LLAssetType::AT_LSL_TEXT:
			case LLAssetType::AT_LSL_BYTECODE:
				gAssetStorage->getInvItemAsset(container != NULL ? container->getRegion()->getHost() : LLHost::invalid,
					gAgent.getID(),
					gAgent.getSessionID(),
					perm.getOwner(),
					container != NULL ? container->getID() : LLUUID::null,
					inv_item->getUUID(),
					inv_item->getAssetUUID(),
					item->getType(),
					JCAssetExportCallback,
					info,
					TRUE
				);
				break;
			case LLAssetType::AT_SOUND:
			case LLAssetType::AT_CLOTHING:
			case LLAssetType::AT_BODYPART:
			case LLAssetType::AT_ANIMATION:
			case LLAssetType::AT_GESTURE:
			default:
				gAssetStorage->getAssetData(inv_item->getAssetUUID(), inv_item->getType(), JCAssetExportCallback, info, TRUE);
				break;
			}

			return TRUE;

			//gAssetStorage->getAssetData(asset_id, item->getType(), JCAssetExportCallback, info,1);
		}
	}
	return FALSE;
}


void JCExportTracker::cleanup()
{
	gIdleCallbacks.deleteFunction(exportworker);
	gIdleCallbacks.deleteFunction(propertyworker);
	
	mRequestedTextures.clear();
	requested_properties.clear();

	std::list<InventoryRequest_t*>::iterator iter3=requested_inventory.begin();
	for(;iter3!=requested_inventory.end();iter3++)
	{
		(*iter3)->object->removeInventoryListener(sInstance);
	}

	requested_inventory.clear();

	//we're probably done with the surrogate objects, magic them away
	removeSurrogates();
	
	std::list<LLSD *>::iterator iter=processed_prims.begin();
	for(;iter!=processed_prims.end();iter++)
	{
		free((*iter));
	}

	processed_prims.clear();

	std::map<LLUUID,LLSD *>::iterator iter4=received_properties.begin();
	for(;iter4!=received_properties.end();iter4++)
	{
		free((*iter4).second);
	}
	received_properties.clear();

	std::map<LLUUID,LLSD *>::iterator iter2=received_inventory.begin();
	for(;iter2!=received_inventory.begin();iter2++)
	{
		free((*iter2).second);
	}
	received_inventory.clear();

	expected_surrogate_pos.clear();
	queued_surrogates.clear();
}

BOOL zip_folder(const std::string& srcfile, const std::string& dstfile)
{
	const S32 COMPRESS_BUFFER_SIZE = 32768;
	std::string tmpfile;
	BOOL retval = FALSE;
	U8 buffer[COMPRESS_BUFFER_SIZE];
	gzFile dst = NULL;
	LLFILE *src = NULL;
	S32 bytes = 0;
	tmpfile = dstfile + ".t";
	dst = gzopen(tmpfile.c_str(), "wb");		/* Flawfinder: ignore */
	if (! dst) goto err;
	src = LLFile::fopen(srcfile, "rb");		/* Flawfinder: ignore */
	if (! src) goto err;

	do
	{
		bytes = (S32)fread(buffer, sizeof(U8), COMPRESS_BUFFER_SIZE,src);
		gzwrite(dst, buffer, bytes);
	} while(feof(src) == 0);
	gzclose(dst);
	dst = NULL;
#if LL_WINDOWS
	// Rename in windows needs the dstfile to not exist.
	LLFile::remove(dstfile);
#endif
	if (LLFile::rename(tmpfile, dstfile) == -1) goto err;		/* Flawfinder: ignore */
	retval = TRUE;
 err:
	if (src != NULL) fclose(src);
	if (dst != NULL) gzclose(dst);
	return retval;
}
