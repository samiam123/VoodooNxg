/** 
 * @file llurlrequest.cpp
 * @author Phoenix
 * @date 2005-04-28
 * @brief Implementation of the URLRequest class and related classes.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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

#include "linden_common.h"
#include "llurlrequest.h"

#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

#include <algorithm>
#include <openssl/x509_vfy.h>
#include <openssl/ssl.h>

#include "llcurl.h"
#include "llioutil.h"
#include "llmemtype.h"
#include "llpumpio.h"
#include "llsd.h"
#include "llstring.h"
#include "apr_env.h"
#include "llapr.h"
#include "llscopedvolatileaprpool.h"
#include "llfasttimer.h"
static const U32 HTTP_STATUS_PIPE_ERROR = 499;

/**
 * String constants
 */
const std::string CONTEXT_TRANSFERED_BYTES("transfered_bytes");


static size_t headerCallback(char* data, size_t size, size_t nmemb, void* user);

/**
 * class LLURLRequestDetail
 */
class LLURLRequestDetail
{
public:
	LLURLRequestDetail();
	~LLURLRequestDetail();
	std::string mURL;
	AICurlEasyRequest mCurlEasyRequest;
	LLIOPipe::buffer_ptr_t mResponseBuffer;
	LLChannelDescriptors mChannels;
	U8* mLastRead;
	U32 mBodyLimit;
	S32 mByteAccumulator;
	bool mIsBodyLimitSet;
};

LLURLRequestDetail::LLURLRequestDetail() :
	mCurlEasyRequest(false),
	mLastRead(NULL),
	mBodyLimit(0),
	mByteAccumulator(0),
	mIsBodyLimitSet(false)
{
}

LLURLRequestDetail::~LLURLRequestDetail()
{
	mLastRead = NULL;
}

/**
 * class LLURLRequest
 */

// static
std::string LLURLRequest::actionAsVerb(LLURLRequest::ERequestAction action)
{
	static const std::string VERBS[] =
	{
		"(invalid)",
		"HEAD",
		"GET",
		"PUT",
		"POST",
		"DELETE",
		"MOVE"
	};
	if(((S32)action <=0) || ((S32)action >= REQUEST_ACTION_COUNT))
	{
		return VERBS[0];
	}
	return VERBS[action];
}

LLURLRequest::LLURLRequest(LLURLRequest::ERequestAction action) :
	mAction(action)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	// This might throw AICurlNoEasyHandle.
	initialize();
}

LLURLRequest::LLURLRequest(
	LLURLRequest::ERequestAction action,
	const std::string& url) :
	mAction(action)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	// This might throw AICurlNoEasyHandle.
	initialize();
	setURL(url);
}

LLURLRequest::~LLURLRequest()
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	{
		AICurlEasyRequest_wat curl_easy_request_w(*mDetail->mCurlEasyRequest);
		curl_easy_request_w->revokeCallbacks();
		curl_easy_request_w->send_events_to(NULL);
	}
	delete mDetail;
}

void LLURLRequest::setURL(const std::string& url)
{
	mDetail->mURL = url;
}

std::string LLURLRequest::getURL() const
{
	return mDetail->mURL;
}

void LLURLRequest::addHeader(const char* header)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
	curlEasyRequest_w->addHeader(header);
}

void LLURLRequest::checkRootCertificate(bool check)
{
	AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
	curlEasyRequest_w->setopt(CURLOPT_SSL_VERIFYPEER, check ? 1L : 0L);
	curlEasyRequest_w->setoptString(CURLOPT_ENCODING, "");
}

void LLURLRequest::setBodyLimit(U32 size)
{
	mDetail->mBodyLimit = size;
	mDetail->mIsBodyLimitSet = true;
}

void LLURLRequest::setCallback(LLURLRequestComplete* callback)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	mCompletionCallback = callback;
	AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
	curlEasyRequest_w->setHeaderCallback(&headerCallback, (void*)callback);
}

// Added to mitigate the effect of libcurl looking
// for the ALL_PROXY and http_proxy env variables
// and deciding to insert a Pragma: no-cache
// header! The only usage of this method at the
// time of this writing is in llhttpclient.cpp
// in the request() method, where this method
// is called with use_proxy = FALSE
void LLURLRequest::useProxy(bool use_proxy)
{
    static std::string env_proxy;

    if (use_proxy && env_proxy.empty())
    {
		char* env_proxy_str;
        LLScopedVolatileAPRPool scoped_pool;
        apr_status_t status = apr_env_get(&env_proxy_str, "ALL_PROXY", scoped_pool);
        if (status != APR_SUCCESS)
        {
			status = apr_env_get(&env_proxy_str, "http_proxy", scoped_pool);
        }
        if (status != APR_SUCCESS)
        {
            use_proxy = false;
        }
		else
		{
			// env_proxy_str is stored in the scoped_pool, so we have to make a copy.
			env_proxy = env_proxy_str;
		}
    }

	LL_DEBUGS("Proxy") << "use_proxy = " << (use_proxy?'Y':'N') << ", env_proxy = " << (!env_proxy.empty() ? env_proxy : "(null)") << LL_ENDL;

	AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
	curlEasyRequest_w->setoptString(CURLOPT_PROXY, (use_proxy && !env_proxy.empty()) ? env_proxy : std::string(""));
}

void LLURLRequest::useProxy(const std::string &proxy)
{
	AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
    curlEasyRequest_w->setoptString(CURLOPT_PROXY, proxy);
}

void LLURLRequest::allowCookies()
{
	AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
	curlEasyRequest_w->setoptString(CURLOPT_COOKIEFILE, "");
}

//virtual 
bool LLURLRequest::hasExpiration(void) const
{
	// Currently, this ALWAYS returns false -- because only AICurlEasyRequestStateMachine uses buffered
	// AICurlEasyRequest objects, and LLURLRequest uses (unbuffered) AICurlEasyRequest directly, which
	// have no expiration facility.
	return mDetail->mCurlEasyRequest.isBuffered();
}

//virtual 
bool LLURLRequest::hasNotExpired(void) const
{
	if (!mDetail->mCurlEasyRequest.isBuffered())
	  return true;
	AICurlEasyRequest_wat buffered_easy_request_w(*mDetail->mCurlEasyRequest);
	AICurlResponderBuffer_wat buffer_w(*mDetail->mCurlEasyRequest);
	return buffer_w->isValid();
}

// virtual
LLIOPipe::EStatus LLURLRequest::handleError(
	LLIOPipe::EStatus status,
	LLPumpIO* pump)
{
	DoutEntering(dc::curl, "LLURLRequest::handleError(" << LLIOPipe::lookupStatusString(status) << ", " << (void*)pump << ")");
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);

	if (LL_LIKELY(!mDetail->mCurlEasyRequest.isBuffered()))	// Currently always true.
	{
		// The last reference will be deleted when the pump that this chain belongs to
		// is removed from the running chains vector, upon returning from this function.
		// This keeps the CurlEasyRequest object alive until the curl thread cleanly removed it.
		Dout(dc::curl, "Calling mDetail->mCurlEasyRequest.removeRequest()");
		mDetail->mCurlEasyRequest.removeRequest();
	}
	else if (!hasNotExpired())
	{
		// The buffered version has it's own time out handling, and that already expired,
		// so we can ignore the expiration of this timer (currently never happens).
		// I left it here because it's what LL did (in the form if (!isValid() ...),
		// and it would be relevant if this characteristic of mDetail->mCurlEasyRequest
		// would change. --Aleric
		return STATUS_EXPIRED ;
	}

	if(mCompletionCallback && pump)
	{
		LLURLRequestComplete* complete = NULL;
		complete = (LLURLRequestComplete*)mCompletionCallback.get();
		complete->httpStatus(
			HTTP_STATUS_PIPE_ERROR,
			LLIOPipe::lookupStatusString(status));
		complete->responseStatus(status);
		pump->respond(complete);
		mCompletionCallback = NULL;
	}
	return status;
}

void LLURLRequest::added_to_multi_handle(AICurlEasyRequest_wat&)
{
}

void LLURLRequest::finished(AICurlEasyRequest_wat&)
{
}

void LLURLRequest::removed_from_multi_handle(AICurlEasyRequest_wat&)
{
	mRemoved = true;
}

static LLFastTimer::DeclareTimer FTM_PROCESS_URL_REQUEST("URL Request");
static LLFastTimer::DeclareTimer FTM_PROCESS_URL_REQUEST_GET_RESULT("Get Result");

// virtual
LLIOPipe::EStatus LLURLRequest::process_impl(
	const LLChannelDescriptors& channels,
	buffer_ptr_t& buffer,
	bool& eos,
	LLSD& context,
	LLPumpIO* pump)
{
	LLFastTimer t(FTM_PROCESS_URL_REQUEST);
	PUMP_DEBUG;
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	//llinfos << "LLURLRequest::process_impl()" << llendl;
	if (!buffer) return STATUS_ERROR;

	if (!mDetail) return STATUS_ERROR; //Seems to happen on occasion. Need to hunt down why.

	// we're still waiting or processing, check how many
	// bytes we have accumulated.
	const S32 MIN_ACCUMULATION = 100000;
	if(pump && (mDetail->mByteAccumulator > MIN_ACCUMULATION))
	{
		static LLFastTimer::DeclareTimer FTM_URL_ADJUST_TIMEOUT("Adjust Timeout");
		LLFastTimer t(FTM_URL_ADJUST_TIMEOUT);
		 // This is a pretty sloppy calculation, but this
		 // tries to make the gross assumption that if data
		 // is coming in at 56kb/s, then this transfer will
		 // probably succeed. So, if we're accumlated
		 // 100,000 bytes (MIN_ACCUMULATION) then let's
		 // give this client another 2s to complete.
		 const F32 TIMEOUT_ADJUSTMENT = 2.0f;
		 mDetail->mByteAccumulator = 0;
		 pump->adjustTimeoutSeconds(TIMEOUT_ADJUSTMENT);
		 lldebugs << "LLURLRequest adjustTimeoutSeconds for request: " << mDetail->mURL << llendl;
		 if (mState == STATE_INITIALIZED)
		 {
			  llinfos << "LLURLRequest adjustTimeoutSeconds called during upload" << llendl;
		 }
	}

	switch(mState)
	{
	case STATE_INITIALIZED:
	{
		PUMP_DEBUG;
		// We only need to wait for input if we are uploading
		// something.
		if(((HTTP_PUT == mAction) || (HTTP_POST == mAction)) && !eos)
		{
			// we're waiting to get all of the information
			return STATUS_BREAK;
		}

		// *FIX: bit of a hack, but it should work. The configure and
		// callback method expect this information to be ready.
		mDetail->mResponseBuffer = buffer;
		mDetail->mChannels = channels;
		if(!configure())
		{
			return STATUS_ERROR;
		}
		mRemoved = false;
		mState = STATE_WAITING_FOR_RESPONSE;
		mDetail->mCurlEasyRequest.addRequest();		// Add easy handle to multi handle.

		return STATUS_BREAK;
	}
	case STATE_WAITING_FOR_RESPONSE:
	case STATE_PROCESSING_RESPONSE:
	{
		if (!mRemoved)								// Not removed from multi handle yet?
		{
			// Easy handle is still being processed.
			return STATUS_BREAK;
		}
		// Curl thread finished with this easy handle.
		mState = STATE_CURL_FINISHED;
	}
	case STATE_CURL_FINISHED:
	{
		PUMP_DEBUG;
		LLIOPipe::EStatus status = STATUS_NO_CONNECTION;	// Catch-all failure code.

		// Left braces in order not to change indentation.
		{
			CURLcode result;

				LLFastTimer t(FTM_PROCESS_URL_REQUEST_GET_RESULT);

			AICurlEasyRequest_wat(*mDetail->mCurlEasyRequest)->getResult(&result);
		
			mState = STATE_HAVE_RESPONSE;
			context[CONTEXT_REQUEST][CONTEXT_TRANSFERED_BYTES] = mRequestTransferedBytes;
			context[CONTEXT_RESPONSE][CONTEXT_TRANSFERED_BYTES] = mResponseTransferedBytes;
			lldebugs << this << "Setting context to " << context << llendl;
			switch(result)
			{
				case CURLE_OK:
				case CURLE_WRITE_ERROR:
					// NB: The error indication means that we stopped the
					// writing due the body limit being reached
					if(mCompletionCallback && pump)
					{
						LLURLRequestComplete* complete = NULL;
						complete = (LLURLRequestComplete*)
							mCompletionCallback.get();
						complete->responseStatus(
								result == CURLE_OK
									? STATUS_OK : STATUS_STOP);
						LLPumpIO::links_t chain;
						LLPumpIO::LLLinkInfo link;
						link.mPipe = mCompletionCallback;
						link.mChannels = LLBufferArray::makeChannelConsumer(
							channels);
						chain.push_back(link);
						static LLFastTimer::DeclareTimer FTM_PROCESS_URL_PUMP_RESPOND("Pump Respond");
						{
							LLFastTimer t(FTM_PROCESS_URL_PUMP_RESPOND);
							pump->respond(chain, buffer, context);
						}
						mCompletionCallback = NULL;
					}
					status = STATUS_BREAK;		// This is what the old code returned. Does it make sense?
					break;
				case CURLE_FAILED_INIT:
				case CURLE_COULDNT_CONNECT:
					status = STATUS_NO_CONNECTION;
					break;
				default:
					llwarns << "URLRequest Error: " << result
							<< ", "
							<< LLCurl::strerror(result)
							<< ", "
							<< (mDetail->mURL.empty() ? "<EMPTY URL>" : mDetail->mURL)
							<< llendl;
					status = STATUS_ERROR;
					break;
			}
		}
		return status;
	}
	case STATE_HAVE_RESPONSE:
		PUMP_DEBUG;
		// we already stuffed everything into channel in in the curl
		// callback, so we are done.
		eos = true;
		context[CONTEXT_REQUEST][CONTEXT_TRANSFERED_BYTES] = mRequestTransferedBytes;
		context[CONTEXT_RESPONSE][CONTEXT_TRANSFERED_BYTES] = mResponseTransferedBytes;
		lldebugs << this << "Setting context to " << context << llendl;
		return STATUS_DONE;

	default:
		PUMP_DEBUG;
		context[CONTEXT_REQUEST][CONTEXT_TRANSFERED_BYTES] = mRequestTransferedBytes;
		context[CONTEXT_RESPONSE][CONTEXT_TRANSFERED_BYTES] = mResponseTransferedBytes;
		lldebugs << this << "Setting context to " << context << llendl;
		return STATUS_ERROR;
	}
}

void LLURLRequest::initialize()
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	mState = STATE_INITIALIZED;
	// This might throw AICurlNoEasyHandle.
	mDetail = new LLURLRequestDetail;

	{
		AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
		curlEasyRequest_w->setWriteCallback(&downCallback, (void*)this);
		curlEasyRequest_w->setReadCallback(&upCallback, (void*)this);
	}

	mRequestTransferedBytes = 0;
	mResponseTransferedBytes = 0;
}

static LLFastTimer::DeclareTimer FTM_URL_REQUEST_CONFIGURE("URL Configure");
bool LLURLRequest::configure()
{
	LLFastTimer t(FTM_URL_REQUEST_CONFIGURE);
	
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	bool rv = false;
	S32 bytes = mDetail->mResponseBuffer->countAfter(
   		mDetail->mChannels.in(),
		NULL);
	{
		AICurlEasyRequest_wat curlEasyRequest_w(*mDetail->mCurlEasyRequest);
		switch(mAction)
		{
		case HTTP_HEAD:
			curlEasyRequest_w->setopt(CURLOPT_HEADER, 1);
			curlEasyRequest_w->setopt(CURLOPT_NOBODY, 1);
			curlEasyRequest_w->setopt(CURLOPT_FOLLOWLOCATION, 1);
			rv = true;
			break;

		case HTTP_GET:
			curlEasyRequest_w->setopt(CURLOPT_HTTPGET, 1);
			curlEasyRequest_w->setopt(CURLOPT_FOLLOWLOCATION, 1);

			// Set Accept-Encoding to allow response compression
			curlEasyRequest_w->setoptString(CURLOPT_ENCODING, "");
			rv = true;
			break;

		case HTTP_PUT:
			// Disable the expect http 1.1 extension. POST and PUT default
			// to turning this on, and I am not too sure what it means.
			curlEasyRequest_w->addHeader("Expect:");
			curlEasyRequest_w->setopt(CURLOPT_UPLOAD, 1);
			curlEasyRequest_w->setopt(CURLOPT_INFILESIZE, bytes);
			rv = true;
			break;

		case HTTP_POST:
			// Set the handle for an http post
			curlEasyRequest_w->setPost(bytes);

			// Set Accept-Encoding to allow response compression
			curlEasyRequest_w->setoptString(CURLOPT_ENCODING, "");
			rv = true;
			break;

		case HTTP_DELETE:
			// Set the handle for an http post
			curlEasyRequest_w->setoptString(CURLOPT_CUSTOMREQUEST, "DELETE");
			rv = true;
			break;

		case HTTP_MOVE:
			// Set the handle for an http post
			curlEasyRequest_w->setoptString(CURLOPT_CUSTOMREQUEST, "MOVE");
			// *NOTE: should we check for the Destination header?
			rv = true;
			break;

		default:
			llwarns << "Unhandled URLRequest action: " << mAction << llendl;
			break;
		}
		if(rv)
		{
			curlEasyRequest_w->finalizeRequest(mDetail->mURL);
			curlEasyRequest_w->send_events_to(this);
		}
	}
	return rv;
}

// static
size_t LLURLRequest::downCallback(
	char* data,
	size_t size,
	size_t nmemb,
	void* user)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	LLURLRequest* req = (LLURLRequest*)user;
	if(STATE_WAITING_FOR_RESPONSE == req->mState)
	{
		req->mState = STATE_PROCESSING_RESPONSE;
	}
	U32 bytes = size * nmemb;
	if (req->mDetail->mIsBodyLimitSet)
	{
		if (bytes > req->mDetail->mBodyLimit)
		{
			bytes = req->mDetail->mBodyLimit;
			req->mDetail->mBodyLimit = 0;
		}
		else
		{
			req->mDetail->mBodyLimit -= bytes;
		}
	}

	req->mDetail->mResponseBuffer->append(
		req->mDetail->mChannels.out(),
		(U8*)data,
		bytes);
	req->mResponseTransferedBytes += bytes;
	req->mDetail->mByteAccumulator += bytes;
	return bytes;
}

// static
size_t LLURLRequest::upCallback(
	char* data,
	size_t size,
	size_t nmemb,
	void* user)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	LLURLRequest* req = (LLURLRequest*)user;
	S32 bytes = llmin(
		(S32)(size * nmemb),
		req->mDetail->mResponseBuffer->countAfter(
			req->mDetail->mChannels.in(),
			req->mDetail->mLastRead));
	req->mDetail->mLastRead =  req->mDetail->mResponseBuffer->readAfter(
		req->mDetail->mChannels.in(),
		req->mDetail->mLastRead,
		(U8*)data,
		bytes);
	req->mRequestTransferedBytes += bytes;
	return bytes;
}

static size_t headerCallback(char* header_line, size_t size, size_t nmemb, void* user)
{
	size_t header_len = size * nmemb;
	LLURLRequestComplete* complete = (LLURLRequestComplete*)user;

	if (!complete || !header_line)
	{
		return header_len;
	}

	// *TODO: This should be a utility in llstring.h: isascii()
	for (size_t i = 0; i < header_len; ++i)
	{
		if (header_line[i] < 0)
		{
			return header_len;
		}
	}

	std::string header(header_line, header_len);

	// Per HTTP spec the first header line must be the status line.
	if (header.substr(0,5) == "HTTP/")
	{
		std::string::iterator end = header.end();
		std::string::iterator pos1 = std::find(header.begin(), end, ' ');
		if (pos1 != end) ++pos1;
		std::string::iterator pos2 = std::find(pos1, end, ' ');
		if (pos2 != end) ++pos2;
		std::string::iterator pos3 = std::find(pos2, end, '\r');

		std::string version(header.begin(), pos1);
		std::string status(pos1, pos2);
		std::string reason(pos2, pos3);

		S32 status_code = atoi(status.c_str());
		if (status_code > 0)
		{
			complete->httpStatus((U32)status_code, reason);
			return header_len;
		}
	}

	std::string::iterator sep = std::find(header.begin(),header.end(),':');

	if (sep != header.end())
	{
		std::string key(header.begin(), sep);
		std::string value(sep + 1, header.end());

		key = utf8str_tolower(utf8str_trim(key));
		value = utf8str_trim(value);

		complete->header(key, value);
	}
	else
	{
		LLStringUtil::trim(header);
		if (!header.empty())
		{
			llwarns << "Unable to parse header: " << header << llendl;
		}
	}

	return header_len;
}

/**
 * LLURLRequestComplete
 */
LLURLRequestComplete::LLURLRequestComplete() :
	mRequestStatus(LLIOPipe::STATUS_ERROR)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
}

// virtual
LLURLRequestComplete::~LLURLRequestComplete()
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
}

//virtual 
void LLURLRequestComplete::header(const std::string& header, const std::string& value)
{
}

//virtual 
void LLURLRequestComplete::complete(const LLChannelDescriptors& channels,
		const buffer_ptr_t& buffer)
{
	if(STATUS_OK == mRequestStatus)
	{
		response(channels, buffer);
	}
	else
	{
		noResponse();
	}
}

//virtual 
void LLURLRequestComplete::response(const LLChannelDescriptors& channels,
		const buffer_ptr_t& buffer)
{
	llwarns << "LLURLRequestComplete::response default implementation called"
		<< llendl;
}

//virtual 
void LLURLRequestComplete::noResponse()
{
	llwarns << "LLURLRequestComplete::noResponse default implementation called"
		<< llendl;
}

void LLURLRequestComplete::responseStatus(LLIOPipe::EStatus status)
{
	LLMemType m1(LLMemType::MTYPE_IO_URL_REQUEST);
	mRequestStatus = status;
}

static LLFastTimer::DeclareTimer FTM_PROCESS_URL_COMPLETE("URL Complete");
// virtual
LLIOPipe::EStatus LLURLRequestComplete::process_impl(
	const LLChannelDescriptors& channels,
	buffer_ptr_t& buffer,
	bool& eos,
	LLSD& context,
	LLPumpIO* pump)
{
	LLFastTimer t(FTM_PROCESS_URL_COMPLETE);
	PUMP_DEBUG;
	complete(channels, buffer);
	return STATUS_OK;
}
