/**
 * @file llcurl.cpp
 * @author Zero / Donovan
 * @date 2006-10-15
 * @brief Implementation of wrapper around libcurl.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#if LL_WINDOWS
#define SAFE_SSL 1
#elif LL_DARWIN
#define SAFE_SSL 1
#else
#define SAFE_SSL 1
#endif

#include "linden_common.h"

#include "llcurl.h"

#include <algorithm>
#include <iomanip>
#include <curl/curl.h>
#if SAFE_SSL
#include <openssl/crypto.h>
#endif

#include "llbufferstream.h"
#include "llproxy.h"
#include "llsdserialize.h"
#include "llstl.h"
#include "llthread.h"
#include "lltimer.h"

//////////////////////////////////////////////////////////////////////////////
/*
	The trick to getting curl to do keep-alives is to reuse the
	same easy handle for the requests.  It appears that curl
	keeps a pool of connections alive for each easy handle, but
	doesn't share them between easy handles.  Therefore it is
	important to keep a pool of easy handles and reuse them,
	rather than create and destroy them with each request.  This
	code does this.

	Furthermore, it would behoove us to keep track of which
	hosts an easy handle was used for and pick an easy handle
	that matches the next request.  This code does not current
	do this.
 */

//////////////////////////////////////////////////////////////////////////////

static const U32 EASY_HANDLE_POOL_SIZE		= 5;
static const S32 MULTI_PERFORM_CALL_REPEAT	= 5;
static const S32 CURL_REQUEST_TIMEOUT = 30; // seconds
static const S32 MAX_ACTIVE_REQUEST_COUNT = 100;

static 
// DEBUG //
S32 gCurlEasyCount = 0;
S32 gCurlMultiCount = 0;

//////////////////////////////////////////////////////////////////////////////

//static
std::vector<LLMutex*> LLCurl::sSSLMutex;
std::string LLCurl::sCAPath;
std::string LLCurl::sCAFile;

bool LLCurl::sMultiThreaded = false;
static U32 sMainThreadID = 0;

void check_curl_code(CURLcode code)
{
	if (code != CURLE_OK)
	{
		// linux appears to throw a curl error once per session for a bad initialization
		// at a pretty random time (when enabling cookies).
		llinfos << "curl error detected: " << curl_easy_strerror(code) << llendl;
	}
}

void check_curl_multi_code(CURLMcode code) 
{
	if (code != CURLM_OK)
	{
		// linux appears to throw a curl error once per session for a bad initialization
		// at a pretty random time (when enabling cookies).
		llinfos << "curl multi error detected: " << curl_multi_strerror(code) << llendl;
	}
}

//static
void LLCurl::setCAPath(const std::string& path)
{
	sCAPath = path;
}

//static
void LLCurl::setCAFile(const std::string& file)
{
	sCAFile = file;
}

//static
std::string LLCurl::getVersionString()
{
	return std::string(curl_version());
}

//////////////////////////////////////////////////////////////////////////////

LLCurl::Responder::Responder()
	: mReferenceCount(0)
{
}

LLCurl::Responder::~Responder()
{
}

// virtual
void LLCurl::Responder::errorWithContent(
	U32 status,
	const std::string& reason,
	const LLSD&)
{
	error(status, reason);
}

// virtual
void LLCurl::Responder::error(U32 status, const std::string& reason)
{
	llinfos << mURL << " [" << status << "]: " << reason << llendl;
}

// virtual
void LLCurl::Responder::result(const LLSD& content)
{
}

void LLCurl::Responder::setURL(const std::string& url)
{
	mURL = url;
}

// virtual
void LLCurl::Responder::completedRaw(
	U32 status,
	const std::string& reason,
	const LLChannelDescriptors& channels,
	const LLIOPipe::buffer_ptr_t& buffer)
{
	LLSD content;
	LLBufferStream istr(channels, buffer.get());
	if (!LLSDSerialize::fromXML(content, istr))
	{
		llinfos << "Failed to deserialize LLSD. " << mURL << " [" << status << "]: " << reason << llendl;
	}
	completed(status, reason, content);
}

// virtual
void LLCurl::Responder::completed(U32 status, const std::string& reason, const LLSD& content)
{
	if (isGoodStatus(status))
	{
		result(content);
	}
	else
	{
		errorWithContent(status, reason, content);
	}
}

//virtual
void LLCurl::Responder::completedHeader(U32 status, const std::string& reason, const LLSD& content)
{

}

// DONT UNCOMMENT THIS IT BREAKS GCC47
//namespace boost
//{
	void intrusive_ptr_add_ref(LLCurl::Responder* p)
	{
		++p->mReferenceCount;
	}
	
	void intrusive_ptr_release(LLCurl::Responder* p)
	{
		if(p && 0 == --p->mReferenceCount)
		{
			delete p;
		}
	}
//};


//////////////////////////////////////////////////////////////////////////////

std::set<CURL*> LLCurl::Easy::sFreeHandles;
std::set<CURL*> LLCurl::Easy::sActiveHandles;
LLMutex* LLCurl::Easy::sHandleMutex = NULL;
LLMutex* LLCurl::Easy::sMultiMutex = NULL;

//static
CURL* LLCurl::Easy::allocEasyHandle()
{
	CURL* ret = NULL;
	LLMutexLock lock(sHandleMutex);
	if (sFreeHandles.empty())
	{
		ret = curl_easy_init();
	}
	else
	{
		ret = *(sFreeHandles.begin());
		sFreeHandles.erase(ret);
		curl_easy_reset(ret);
	}

	if (ret)
	{
		sActiveHandles.insert(ret);
	}

	return ret;
}

//static
void LLCurl::Easy::releaseEasyHandle(CURL* handle)
{
	if (!handle)
	{
		llerrs << "handle cannot be NULL!" << llendl;
	}

	LLMutexLock lock(sHandleMutex);
	
	if (sActiveHandles.find(handle) != sActiveHandles.end())
	{
		sActiveHandles.erase(handle);
		sFreeHandles.insert(handle);
	}
	else
	{
		llerrs << "Invalid handle." << llendl;
	}
}

LLCurl::Easy::Easy()
	: mHeaders(NULL),
	  mCurlEasyHandle(NULL)
{
	mErrorBuffer[0] = 0;
}

LLCurl::Easy* LLCurl::Easy::getEasy()
{
	Easy* easy = new Easy();
	easy->mCurlEasyHandle = allocEasyHandle(); 
	
	if (!easy->mCurlEasyHandle)
	{
		// this can happen if we have too many open files (fails in c-ares/ares_init.c)
		llwarns << "allocEasyHandle() returned NULL! Easy handles: " << gCurlEasyCount << " Multi handles: " << gCurlMultiCount << llendl;
		delete easy;
		return NULL;
	}
	
	// set no DNS caching as default for all easy handles. This prevents them adopting a
	// multi handles cache if they are added to one.
	CURLcode result = curl_easy_setopt(easy->mCurlEasyHandle, CURLOPT_DNS_CACHE_TIMEOUT, 10);
	check_curl_code(result);
	result = curl_easy_setopt(easy->mCurlEasyHandle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	check_curl_code(result);

	// Disable SSL/TLS session caching. Some servers refuse to talk to us when session ids are enabled.
	// id.secondlife.com is such a server, when greeted with a SSL HELLO and a session id, it immediatly returns a RST packet and closes
	// the connections.
	// Fixes: FIRE-5368, FIRE-5756, VWR-28039, VWR-28629
	result = curl_easy_setopt(easy->mCurlEasyHandle, CURLOPT_SSL_SESSIONID_CACHE, 0);
	check_curl_code(result);

	++gCurlEasyCount;
	return easy;
}

LLCurl::Easy::~Easy()
{
	releaseEasyHandle(mCurlEasyHandle);
	--gCurlEasyCount;
	curl_slist_free_all(mHeaders);
	for_each(mStrings.begin(), mStrings.end(), DeletePointerArray());
}

void LLCurl::Easy::resetState()
{
 	curl_easy_reset(mCurlEasyHandle);

	if (mHeaders)
	{
		curl_slist_free_all(mHeaders);
		mHeaders = NULL;
	}

	mRequest.str("");
	mRequest.clear();

	mOutput.reset();
	
	mInput.str("");
	mInput.clear();
	
	mErrorBuffer[0] = 0;
	
	mHeaderOutput.str("");
	mHeaderOutput.clear();
}

void LLCurl::Easy::setErrorBuffer()
{
	setopt(CURLOPT_ERRORBUFFER, &mErrorBuffer);
}

const char* LLCurl::Easy::getErrorBuffer()
{
	return mErrorBuffer;
}

void LLCurl::Easy::setCA()
{
	if(!sCAPath.empty())
	{
		setoptString(CURLOPT_CAPATH, sCAPath);
	}
	if(!sCAFile.empty())
	{
		setoptString(CURLOPT_CAINFO, sCAFile);
	}
}

void LLCurl::Easy::setHeaders()
{
	setopt(CURLOPT_HTTPHEADER, mHeaders);
}

void LLCurl::Easy::getTransferInfo(LLCurl::TransferInfo* info)
{
	check_curl_code(curl_easy_getinfo(mCurlEasyHandle, CURLINFO_SIZE_DOWNLOAD, &info->mSizeDownload));
	check_curl_code(curl_easy_getinfo(mCurlEasyHandle, CURLINFO_TOTAL_TIME, &info->mTotalTime));
	check_curl_code(curl_easy_getinfo(mCurlEasyHandle, CURLINFO_SPEED_DOWNLOAD, &info->mSpeedDownload));
}

U32 LLCurl::Easy::report(CURLcode code)
{
	U32 responseCode = 0;	
	std::string responseReason;
	
	if (code == CURLE_OK)
	{
		check_curl_code(curl_easy_getinfo(mCurlEasyHandle, CURLINFO_RESPONSE_CODE, &responseCode));
		// *TODO: get reason from first line of mHeaderOutput
	}
	else
	{
		responseCode = 499;
		responseReason = strerror(code) + " : " + mErrorBuffer;
		setopt(CURLOPT_FRESH_CONNECT, TRUE);
	}
		
	if (mResponder)
	{	
		mResponder->completedRaw(responseCode, responseReason, mChannels, mOutput);
		mResponder = NULL;
	}
	
	resetState();
	return responseCode;
}

// Note: these all assume the caller tracks the value (i.e. keeps it persistant)
void LLCurl::Easy::setopt(CURLoption option, S32 value)
{
	CURLcode result = curl_easy_setopt(mCurlEasyHandle, option, value);
	check_curl_code(result);
}

void LLCurl::Easy::setopt(CURLoption option, void* value)
{
	CURLcode result = curl_easy_setopt(mCurlEasyHandle, option, value);
	check_curl_code(result);
}

void LLCurl::Easy::setopt(CURLoption option, char* value)
{
	CURLcode result = curl_easy_setopt(mCurlEasyHandle, option, value);
	check_curl_code(result);
}

// Note: this copies the string so that the caller does not have to keep it around
void LLCurl::Easy::setoptString(CURLoption option, const std::string& value)
{
	char* tstring = new char[value.length()+1];
	strcpy(tstring, value.c_str());
	mStrings.push_back(tstring);
	CURLcode result = curl_easy_setopt(mCurlEasyHandle, option, tstring);
	check_curl_code(result);
}

void LLCurl::Easy::slist_append(const char* str)
{
	mHeaders = curl_slist_append(mHeaders, str);
}

size_t curlReadCallback(char* data, size_t size, size_t nmemb, void* user_data)
{
	LLCurl::Easy* easy = (LLCurl::Easy*)user_data;
	
	S32 n = size * nmemb;
	S32 startpos = (S32)easy->getInput().tellg();
	easy->getInput().seekg(0, std::ios::end);
	S32 endpos = (S32)easy->getInput().tellg();
	easy->getInput().seekg(startpos, std::ios::beg);
	S32 maxn = endpos - startpos;
	n = llmin(n, maxn);
	easy->getInput().read((char*)data, n);

	return n;
}

size_t curlWriteCallback(char* data, size_t size, size_t nmemb, void* user_data)
{
	LLCurl::Easy* easy = (LLCurl::Easy*)user_data;
	
	S32 n = size * nmemb;
	easy->getOutput()->append(easy->getChannels().in(), (const U8*)data, n);

	return n;
}

size_t curlHeaderCallback(void* data, size_t size, size_t nmemb, void* user_data)
{
	LLCurl::Easy* easy = (LLCurl::Easy*)user_data;
	
	size_t n = size * nmemb;
	easy->getHeaderOutput().write((const char*)data, n);

	return n;
}

void LLCurl::Easy::prepRequest(const std::string& url,
							   const std::vector<std::string>& headers,
							   ResponderPtr responder, S32 time_out, bool post)
{
	resetState();
	
	if (post) setoptString(CURLOPT_ENCODING, "");

	//setopt(CURLOPT_VERBOSE, 1); // useful for debugging
	setopt(CURLOPT_NOSIGNAL, 1);

	setopt(CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

	// Set the CURL options for either Socks or HTTP proxy
	LLProxy::getInstance()->applyProxySettings(this);

	mOutput.reset(new LLBufferArray);
	setopt(CURLOPT_WRITEFUNCTION, (void*)&curlWriteCallback);
	setopt(CURLOPT_WRITEDATA, (void*)this);

	setopt(CURLOPT_READFUNCTION, (void*)&curlReadCallback);
	setopt(CURLOPT_READDATA, (void*)this);
	
	setopt(CURLOPT_HEADERFUNCTION, (void*)&curlHeaderCallback);
	setopt(CURLOPT_HEADERDATA, (void*)this);
	// Allow up to five redirects
	if(responder && responder->followRedir())
	{
		setopt(CURLOPT_FOLLOWLOCATION, 1);
		setopt(CURLOPT_MAXREDIRS, MAX_REDIRECTS);
	}

	setErrorBuffer();
	setCA();

	setopt(CURLOPT_SSL_VERIFYPEER, true);
	
	//don't verify host name so urls with scrubbed host names will work (improves DNS performance)
	setopt(CURLOPT_SSL_VERIFYHOST, 0);
	setopt(CURLOPT_TIMEOUT, llmax(time_out, CURL_REQUEST_TIMEOUT));

	setoptString(CURLOPT_URL, url);

	mResponder = responder;

	if (!post)
	{
		slist_append("Connection: keep-alive");
		slist_append("Keep-alive: 300");
		// Accept and other headers
		for (std::vector<std::string>::const_iterator iter = headers.begin();
			 iter != headers.end(); ++iter)
		{
			slist_append((*iter).c_str());
		}
	}
}

////////////////////////////////////////////////////////////////////////////

LLCurl::Multi::Multi()
	: LLThread("Curl Multi"),
	  mQueued(0),
	  mErrorCount(0),
	  mPerformState(PERFORM_STATE_READY)
{
	mQuitting = false;

	mThreaded = LLCurl::sMultiThreaded && LLThread::currentID() == sMainThreadID;
	if (mThreaded)
	{
		mSignal = new LLCondition;
	}
	else
	{
		mSignal = NULL;
	}

	mCurlMultiHandle = curl_multi_init();
	if (!mCurlMultiHandle)
	{
		llwarns << "curl_multi_init() returned NULL! Easy handles: " << gCurlEasyCount << " Multi handles: " << gCurlMultiCount << llendl;
		mCurlMultiHandle = curl_multi_init();
	}
	
	llassert_always(mCurlMultiHandle);
	++gCurlMultiCount;
}

LLCurl::Multi::~Multi()
{
	llassert(isStopped());

	if (LLCurl::sMultiThreaded)
	{
		LLCurl::Easy::sMultiMutex->lock();
	}

	delete mSignal;
	mSignal = NULL;

	// Clean up active
	for(easy_active_list_t::iterator iter = mEasyActiveList.begin();
		iter != mEasyActiveList.end(); ++iter)
	{
		Easy* easy = *iter;
		check_curl_multi_code(curl_multi_remove_handle(mCurlMultiHandle, easy->getCurlHandle()));
		delete easy;
	}
	mEasyActiveList.clear();
	mEasyActiveMap.clear();
	
	// Clean up freed
	for_each(mEasyFreeList.begin(), mEasyFreeList.end(), DeletePointer());	
	mEasyFreeList.clear();

	check_curl_multi_code(curl_multi_cleanup(mCurlMultiHandle));
	--gCurlMultiCount;

	if (LLCurl::sMultiThreaded)
	{
		LLCurl::Easy::sMultiMutex->unlock();
	}
}

CURLMsg* LLCurl::Multi::info_read(S32* msgs_in_queue)
{
	CURLMsg* curlmsg = curl_multi_info_read(mCurlMultiHandle, msgs_in_queue);
	return curlmsg;
}

void LLCurl::Multi::perform()
{
	if (mThreaded)
	{
		mSignal->lock();
		if (mPerformState == PERFORM_STATE_READY)
		{
			mSignal->signal();
		}
		mSignal->unlock();
	}
	else
	{
		doPerform();
	}	
}

void LLCurl::Multi::run()
{
	llassert(mThreaded);
	
	mSignal->lock();
	while (!mQuitting)
	{
		mSignal->wait();
		mPerformState = PERFORM_STATE_PERFORMING;
		if (!mQuitting)
		{
			LLMutexLock lock(LLCurl::Easy::sMultiMutex);
			doPerform();
		}
	}
	mSignal->unlock();
}

void LLCurl::Multi::doPerform()
{
	S32 q = 0;
	if (mThreaded)
		mSignal->unlock();
	for (S32 call_count = 0;
			call_count < MULTI_PERFORM_CALL_REPEAT;
			call_count += 1)
	{
		CURLMcode code = curl_multi_perform(mCurlMultiHandle, &q);
		if (CURLM_CALL_MULTI_PERFORM != code || q == 0)
		{
			check_curl_multi_code(code);
			break;
		}
	
	}
	if (mThreaded)
		mSignal->lock();
	mQueued = q;
	mPerformState = PERFORM_STATE_COMPLETED;
}

S32 LLCurl::Multi::process()
{
	perform();

	if (mPerformState != PERFORM_STATE_COMPLETED)
	{
		return 0;
	}

	CURLMsg* msg;
	int msgs_in_queue;

	S32 processed = 0;
	while ((msg = info_read(&msgs_in_queue)))
	{
		++processed;
		if (msg->msg == CURLMSG_DONE)
		{
			U32 response = 0;
			easy_active_map_t::iterator iter = mEasyActiveMap.find(msg->easy_handle);
			if (iter != mEasyActiveMap.end())
			{
				Easy* easy = iter->second;
				response = easy->report(msg->data.result);
				removeEasy(easy);
			}
			else
			{
				response = 499;
				// *TODO: change to llwarns
				llerrs << "cleaned up curl request completed!" << llendl;
			}
			if (response >= 400)
			{
				// failure of some sort, inc mErrorCount for debugging and flagging multi for destruction
				++mErrorCount;
			}
		}
	}

	mPerformState = PERFORM_STATE_READY;
	return processed;
}

LLCurl::Easy* LLCurl::Multi::allocEasy()
{
	Easy* easy = 0;

	if (mEasyFreeList.empty())
	{
		easy = Easy::getEasy();
	}
	else
	{
		easy = *(mEasyFreeList.begin());
		mEasyFreeList.erase(easy);
	}
	if (easy)
	{
		mEasyActiveList.insert(easy);
		mEasyActiveMap[easy->getCurlHandle()] = easy;
	}
	return easy;
}

bool LLCurl::Multi::addEasy(Easy* easy)
{
	CURLMcode mcode = curl_multi_add_handle(mCurlMultiHandle, easy->getCurlHandle());
	check_curl_multi_code(mcode);
	//if (mcode != CURLM_OK)
	//{
	//	llwarns << "Curl Error: " << curl_multi_strerror(mcode) << llendl;
	//	return false;
	//}
	return true;
}

void LLCurl::Multi::easyFree(Easy* easy)
{
	mEasyActiveList.erase(easy);
	mEasyActiveMap.erase(easy->getCurlHandle());
	if (mEasyFreeList.size() < EASY_HANDLE_POOL_SIZE)
	{
		easy->resetState();
		mEasyFreeList.insert(easy);
	}
	else
	{
		delete easy;
	}
}

void LLCurl::Multi::removeEasy(Easy* easy)
{
	check_curl_multi_code(curl_multi_remove_handle(mCurlMultiHandle, easy->getCurlHandle()));
	easyFree(easy);
}

//static
std::string LLCurl::strerror(CURLcode errorcode)
{
	return std::string(curl_easy_strerror(errorcode));
}

////////////////////////////////////////////////////////////////////////////
// For generating a simple request for data
// using one multi and one easy per request 

LLCurlRequest::LLCurlRequest() :
	mActiveMulti(NULL),
	mActiveRequestCount(0)
{
	mThreadID = LLThread::currentID();
	mProcessing = FALSE;
}

LLCurlRequest::~LLCurlRequest()
{
	llassert_always(mThreadID == LLThread::currentID());

	//stop all Multi handle background threads
	for (curlmulti_set_t::iterator iter = mMultiSet.begin(); iter != mMultiSet.end(); ++iter)
	{
		LLCurl::Multi* multi = *iter;
		if (multi->mThreaded)
			multi->mSignal->lock();
		multi->mQuitting = true;
		if (multi->mThreaded)
		{
			while (!multi->isStopped())
			{
				multi->mSignal->signal();
				multi->mSignal->unlock();
				apr_sleep(1000);
				multi->mSignal->lock();
			}
		}
		if (multi->mThreaded)
			multi->mSignal->unlock();
	}
	for_each(mMultiSet.begin(), mMultiSet.end(), DeletePointer());
}

void LLCurlRequest::addMulti()
{
	llassert_always(mThreadID == LLThread::currentID());
	LLCurl::Multi* multi = new LLCurl::Multi();
	if (multi->mThreaded)
	{
		multi->start();
	}
	mMultiSet.insert(multi);
	mActiveMulti = multi;
	mActiveRequestCount = 0;
}

LLCurl::Easy* LLCurlRequest::allocEasy()
{
	if (!mActiveMulti ||
		mActiveRequestCount	>= MAX_ACTIVE_REQUEST_COUNT ||
		mActiveMulti->mErrorCount > 0)
	{
		addMulti();
	}
	llassert_always(mActiveMulti);
	++mActiveRequestCount;
	LLCurl::Easy* easy = mActiveMulti->allocEasy();
	return easy;
}

bool LLCurlRequest::addEasy(LLCurl::Easy* easy)
{
	llassert_always(mActiveMulti);
	
	if (mProcessing)
	{
		llerrs << "Posting to a LLCurlRequest instance from within a responder is not allowed (causes DNS timeouts)." << llendl;
	}
	bool res = mActiveMulti->addEasy(easy);
	return res;
}

void LLCurlRequest::get(const std::string& url, LLCurl::ResponderPtr responder)
{
	getByteRange(url, headers_t(), 0, -1, responder);
}
	
bool LLCurlRequest::getByteRange(const std::string& url,
								 const headers_t& headers,
								 S32 offset, S32 length,
								 LLCurl::ResponderPtr responder)
{
	LLCurl::Easy* easy = allocEasy();
	if (!easy)
	{
		return false;
	}
	easy->prepRequest(url, headers, responder);
	easy->setopt(CURLOPT_HTTPGET, 1);
	if (length > 0)
	{
		std::string range = llformat("Range: bytes=%d-%d", offset,offset+length-1);
		easy->slist_append(range.c_str());
	}
	easy->setHeaders();
	bool res = addEasy(easy);
	return res;
}

bool LLCurlRequest::post(const std::string& url,
						 const headers_t& headers,
						 const LLSD& data,
						 LLCurl::ResponderPtr responder, S32 time_out)
{
	LLCurl::Easy* easy = allocEasy();
	if (!easy)
	{
		return false;
	}
	easy->prepRequest(url, headers, responder, time_out);

	LLSDSerialize::toXML(data, easy->getInput());
	S32 bytes = easy->getInput().str().length();
	
	easy->setopt(CURLOPT_POST, 1);
	easy->setopt(CURLOPT_POSTFIELDS, (void*)NULL);
	easy->setopt(CURLOPT_POSTFIELDSIZE, bytes);

	easy->slist_append("Content-Type: application/llsd+xml");
	easy->setHeaders();

	lldebugs << "POSTING: " << bytes << " bytes." << llendl;
	bool res = addEasy(easy);
	return res;
}

bool LLCurlRequest::post(const std::string& url,
						 const headers_t& headers,
						 const std::string& data,
						 LLCurl::ResponderPtr responder, S32 time_out)
{
	LLCurl::Easy* easy = allocEasy();
	if (!easy)
	{
		return false;
	}
	easy->prepRequest(url, headers, responder, time_out);

	easy->getInput().write(data.data(), data.size());
	S32 bytes = easy->getInput().str().length();
	
	easy->setopt(CURLOPT_POST, 1);
	easy->setopt(CURLOPT_POSTFIELDS, (void*)NULL);
	easy->setopt(CURLOPT_POSTFIELDSIZE, bytes);

	easy->slist_append("Content-Type: application/octet-stream");
	easy->setHeaders();

	lldebugs << "POSTING: " << bytes << " bytes." << llendl;
	bool res = addEasy(easy);
	return res;
}

// Note: call once per frame
S32 LLCurlRequest::process()
{
	llassert_always(mThreadID == LLThread::currentID());
	S32 res = 0;

	mProcessing = TRUE;
	for (curlmulti_set_t::iterator iter = mMultiSet.begin();
		 iter != mMultiSet.end(); )
	{
		curlmulti_set_t::iterator curiter = iter++;
		LLCurl::Multi* multi = *curiter;
		S32 tres = multi->process();
		res += tres;
		if (multi != mActiveMulti && tres == 0 && multi->mQueued == 0)
		{
			mMultiSet.erase(curiter);
			if (multi->mThreaded)
				multi->mSignal->lock();
			multi->mQuitting = true;
			if (multi->mThreaded)
			{
				while (!multi->isStopped())
				{
					multi->mSignal->signal();
					multi->mSignal->unlock();
					apr_sleep(1000);
					multi->mSignal->unlock();
				}
			}
			if (multi->mThreaded)
				multi->mSignal->unlock();

			delete multi;
		}
	}
	mProcessing = FALSE;
	return res;
}

S32 LLCurlRequest::getQueued()
{
	llassert_always(mThreadID == LLThread::currentID());
	S32 queued = 0;
	for (curlmulti_set_t::iterator iter = mMultiSet.begin();
		 iter != mMultiSet.end(); )
	{
		curlmulti_set_t::iterator curiter = iter++;
		LLCurl::Multi* multi = *curiter;
		queued += multi->mQueued;
		if (multi->mPerformState != LLCurl::Multi::PERFORM_STATE_READY)
		{
			++queued;
		}
	}
	return queued;
}

////////////////////////////////////////////////////////////////////////////
// For generating one easy request
// associated with a single multi request

LLCurlEasyRequest::LLCurlEasyRequest()
	: mRequestSent(false),
	  mResultReturned(false)
{
	mMulti = new LLCurl::Multi();
	if (mMulti->mThreaded)
	{
		mMulti->start();
	}
	mEasy = mMulti->allocEasy();
	if (mEasy)
	{
		mEasy->setErrorBuffer();
		mEasy->setCA();
	}
}

LLCurlEasyRequest::~LLCurlEasyRequest()
{
	if (mMulti->mThreaded)
		mMulti->mSignal->lock();
	mMulti->mQuitting = true;
	if (mMulti->mThreaded)
	{
		while (!mMulti->isStopped())
		{
			mMulti->mSignal->signal();
			mMulti->mSignal->unlock();
			apr_sleep(1000);
			mMulti->mSignal->lock();
		}
	}
	if (mMulti->mThreaded)
		mMulti->mSignal->unlock();
	delete mMulti;
}
	
void LLCurlEasyRequest::setopt(CURLoption option, S32 value)
{
	if (mEasy)
	{
		mEasy->setopt(option, value);
	}
}

void LLCurlEasyRequest::setoptString(CURLoption option, const std::string& value)
{
	if (mEasy)
	{
		mEasy->setoptString(option, value);
	}
}

void LLCurlEasyRequest::setPost(char* postdata, S32 size)
{
	if (mEasy)
	{
		mEasy->setopt(CURLOPT_POST, 1);
		mEasy->setopt(CURLOPT_POSTFIELDS, postdata);
		mEasy->setopt(CURLOPT_POSTFIELDSIZE, size);
	}
}

void LLCurlEasyRequest::setHeaderCallback(curl_header_callback callback, void* userdata)
{
	if (mEasy)
	{
		mEasy->setopt(CURLOPT_HEADERFUNCTION, (void*)callback);
		mEasy->setopt(CURLOPT_HEADERDATA, userdata); // aka CURLOPT_WRITEHEADER
	}
}

void LLCurlEasyRequest::setWriteCallback(curl_write_callback callback, void* userdata)
{
	if (mEasy)
	{
		mEasy->setopt(CURLOPT_WRITEFUNCTION, (void*)callback);
		mEasy->setopt(CURLOPT_WRITEDATA, userdata);
	}
}

void LLCurlEasyRequest::setReadCallback(curl_read_callback callback, void* userdata)
{
	if (mEasy)
	{
		mEasy->setopt(CURLOPT_READFUNCTION, (void*)callback);
		mEasy->setopt(CURLOPT_READDATA, userdata);
	}
}

void LLCurlEasyRequest::setSSLCtxCallback(curl_ssl_ctx_callback callback, void* userdata)
{
	if (mEasy)
	{
		mEasy->setopt(CURLOPT_SSL_CTX_FUNCTION, (void*)callback);
		mEasy->setopt(CURLOPT_SSL_CTX_DATA, userdata);
	}
}

void LLCurlEasyRequest::slist_append(const char* str)
{
	if (mEasy)
	{
		mEasy->slist_append(str);
	}
}

void LLCurlEasyRequest::sendRequest(const std::string& url)
{
	llassert_always(!mRequestSent);
	mRequestSent = true;
	lldebugs << url << llendl;
	if (mEasy)
	{
		mEasy->setHeaders();
		mEasy->setoptString(CURLOPT_URL, url);
		mMulti->addEasy(mEasy);
	}
}

void LLCurlEasyRequest::requestComplete()
{
	llassert_always(mRequestSent);
	mRequestSent = false;
	if (mEasy)
	{
		mMulti->removeEasy(mEasy);
	}
}

void LLCurlEasyRequest::perform()
{
	mMulti->perform();
}

// Usage: Call getRestult until it returns false (no more messages)
bool LLCurlEasyRequest::getResult(CURLcode* result, LLCurl::TransferInfo* info)
{
	if (mMulti->mPerformState != LLCurl::Multi::PERFORM_STATE_COMPLETED)
	{ //we're busy, try again later
		return false;
	}
	mMulti->mPerformState = LLCurl::Multi::PERFORM_STATE_READY;

	if (!mEasy)
	{
		// Special case - we failed to initialize a curl_easy (can happen if too many open files)
		//  Act as though the request failed to connect
		if (mResultReturned)
		{
			return false;
		}
		else
		{
			*result = CURLE_FAILED_INIT;
			mResultReturned = true;
			return true;
		}
	}
	// In theory, info_read might return a message with a status other than CURLMSG_DONE
	// In practice for all messages returned, msg == CURLMSG_DONE
	// Ignore other messages just in case
	while(1)
	{
		S32 q;
		CURLMsg* curlmsg = info_read(&q, info);
		if (curlmsg)
		{
			if (curlmsg->msg == CURLMSG_DONE)
			{
				*result = curlmsg->data.result;			
				return true;
			}
			// else continue
		}
		else
		{
			return false;
		}
	}
}

// private
CURLMsg* LLCurlEasyRequest::info_read(S32* q, LLCurl::TransferInfo* info)
{
	if (mEasy)
	{
		CURLMsg* curlmsg = mMulti->info_read(q);
		if (curlmsg && curlmsg->msg == CURLMSG_DONE)
		{
			if (info)
			{
				mEasy->getTransferInfo(info);
			}
		}
		return curlmsg;
	}
	return NULL;
}

std::string LLCurlEasyRequest::getErrorString()
{
	return mEasy ? std::string(mEasy->getErrorBuffer()) : std::string();
}

////////////////////////////////////////////////////////////////////////////

#if SAFE_SSL
//static
void LLCurl::ssl_locking_callback(int mode, int type, const char *file, int line)
{
	if (mode & CRYPTO_LOCK)
	{
		LLCurl::sSSLMutex[type]->lock();
	}
	else
	{
		LLCurl::sSSLMutex[type]->unlock();
	}
}

//static
unsigned long LLCurl::ssl_thread_id(void)
{
	return LLThread::currentID();
}
#endif

void LLCurl::initClass(bool multi_threaded)
{
	sMainThreadID = LLThread::currentID();
	sMultiThreaded = multi_threaded;
	// Do not change this "unless you are familiar with and mean to control 
	// internal operations of libcurl"
	// - http://curl.haxx.se/libcurl/c/curl_global_init.html
	CURLcode code = curl_global_init(CURL_GLOBAL_ALL);

	check_curl_code(code);
	
	Easy::sHandleMutex = new LLMutex;
	Easy::sMultiMutex = new LLMutex;

#if SAFE_SSL
	S32 mutex_count = CRYPTO_num_locks();
	for (S32 i=0; i<mutex_count; i++)
	{
		sSSLMutex.push_back(new LLMutex);
	}
	CRYPTO_set_id_callback(&LLCurl::ssl_thread_id);
	CRYPTO_set_locking_callback(&LLCurl::ssl_locking_callback);
#endif
}

void LLCurl::cleanupClass()
{
#if SAFE_SSL
	CRYPTO_set_locking_callback(NULL);
	for_each(sSSLMutex.begin(), sSSLMutex.end(), DeletePointer());
#endif

	delete Easy::sHandleMutex;
	Easy::sHandleMutex = NULL;
	delete Easy::sMultiMutex;
	Easy::sMultiMutex = NULL;

	for (std::set<CURL*>::iterator iter = Easy::sFreeHandles.begin(); iter != Easy::sFreeHandles.end(); ++iter)
	{
		CURL* curl = *iter;
		curl_easy_cleanup(curl);
	}

	Easy::sFreeHandles.clear();

	llassert(Easy::sActiveHandles.empty());
}

const unsigned int LLCurl::MAX_REDIRECTS = 5;

// Provide access to LLCurl free functions outside of llcurl.cpp without polluting the global namespace.
void LLCurlFF::check_easy_code(CURLcode code)
{
	check_curl_code(code);
}
void LLCurlFF::check_multi_code(CURLMcode code)
{
	check_curl_multi_code(code);
}
