/** 
 * @file llmemory.h
 * @brief Memory allocation/deallocation header-stuff goes here.
 *
 * $LicenseInfo:firstyear=2002&license=viewergpl$
 * 
 * Copyright (c) 2002-2009, Linden Research, Inc.
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
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
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
#ifndef LLMEMORY_H
#define LLMEMORY_H


#include <new>
#include <cstdlib>
#if !LL_WINDOWS
#include <stdint.h>		// uintptr_t
#endif

#include "llerror.h"
#include "llmemtype.h"
#if LL_DEBUG
inline void* ll_aligned_malloc( size_t size, int align )
{
	void* mem = malloc( size + (align - 1) + sizeof(void*) );
	char* aligned = ((char*)mem) + sizeof(void*);
	aligned += align - ((uintptr_t)aligned & (align - 1));

	((void**)aligned)[-1] = mem;
	return aligned;
}

inline void ll_aligned_free( void* ptr )
{
	free( ((void**)ptr)[-1] );
}

inline void* ll_aligned_malloc_16(size_t size) // returned hunk MUST be freed with ll_aligned_free_16().
{
#if defined(LL_WINDOWS)
	return _mm_malloc(size, 16);
#elif defined(LL_DARWIN)
	return malloc(size); // default osx malloc is 16 byte aligned.
#else
	void *rtn;
	if (LL_LIKELY(0 == posix_memalign(&rtn, 16, size)))
		return rtn;
	else // bad alignment requested, or out of memory
		return NULL;
#endif
}

inline void ll_aligned_free_16(void *p)
{
#if defined(LL_WINDOWS)
	_mm_free(p);
#elif defined(LL_DARWIN)
	return free(p);
#else
	free(p); // posix_memalign() is compatible with heap deallocator
#endif
}

inline void* ll_aligned_malloc_32(size_t size) // returned hunk MUST be freed with ll_aligned_free_32().
{
#if defined(LL_WINDOWS)
	return _mm_malloc(size, 32);
#elif defined(LL_DARWIN)
	return ll_aligned_malloc( size, 32 );
#else
	void *rtn;
	if (LL_LIKELY(0 == posix_memalign(&rtn, 32, size)))
		return rtn;
	else // bad alignment requested, or out of memory
		return NULL;
#endif
}

inline void ll_aligned_free_32(void *p)
{
#if defined(LL_WINDOWS)
	_mm_free(p);
#elif defined(LL_DARWIN)
	ll_aligned_free( p );
#else
	free(p); // posix_memalign() is compatible with heap deallocator
#endif
}
#else // LL_DEBUG
// ll_aligned_foo are noops now that we use tcmalloc everywhere (tcmalloc aligns automatically at appropriate intervals)
#define ll_aligned_malloc( size, align ) malloc(size)
#define ll_aligned_free( ptr ) free(ptr)
#define ll_aligned_malloc_16 malloc
#define ll_aligned_free_16 free
#define ll_aligned_malloc_32 malloc
#define ll_aligned_free_32 free
#endif // LL_DEBUG

#ifndef __DEBUG_PRIVATE_MEM__
#define __DEBUG_PRIVATE_MEM__  0
#endif

class LL_COMMON_API LLMemory
{
public:
	static void initClass();
	static void cleanupClass();
	static void freeReserve();
	// Return the resident set size of the current process, in bytes.
	// Return value is zero if not known.
	static U64 getCurrentRSS();
	static U32 getWorkingSetSize();
	static void* tryToAlloc(void* address, U32 size);
	static void initMaxHeapSizeGB(F32 max_heap_size_gb, BOOL prevent_heap_failure);
	static void updateMemoryInfo() ;
	static void logMemoryInfo(BOOL update = FALSE);
	static bool isMemoryPoolLow();

	static U32 getAvailableMemKB() ;
	static U32 getMaxMemKB() ;
	static U32 getAllocatedMemKB() ;
private:
	static char* reserveMem;
	static U32 sAvailPhysicalMemInKB ;
	static U32 sMaxPhysicalMemInKB ;
	static U32 sAllocatedMemInKB;
	static U32 sAllocatedPageSizeInKB ;

	static U32 sMaxHeapSizeInKB;
	static BOOL sEnableMemoryFailurePrevention;
};

//----------------------------------------------------------------------------
class LLMutex ;
#if MEM_TRACK_MEM
class LL_COMMON_API LLMemTracker
{
private:
	LLMemTracker() ;
	~LLMemTracker() ;

public:
	static void release() ;
	static LLMemTracker* getInstance() ;

	void track(const char* function, const int line) ;
	void preDraw(BOOL pause) ;
	void postDraw() ;
	const char* getNextLine() ;

private:
	static LLMemTracker* sInstance ;
	
	char**     mStringBuffer ;
	S32        mCapacity ;
	U32        mLastAllocatedMem ;
	S32        mCurIndex ;
	S32        mCounter;
	S32        mDrawnIndex;
	S32        mNumOfDrawn;
	BOOL       mPaused;
	LLMutex*   mMutexp ;
};

#define MEM_TRACK_RELEASE LLMemTracker::release() ;
#define MEM_TRACK         LLMemTracker::getInstance()->track(__FUNCTION__, __LINE__) ;

#else // MEM_TRACK_MEM

#define MEM_TRACK_RELEASE
#define MEM_TRACK

#endif // MEM_TRACK_MEM

//----------------------------------------------------------------------------


//
//class LLPrivateMemoryPool defines a private memory pool for an application to use, so the application does not
//need to access the heap directly fro each memory allocation. Throught this, the allocation speed is faster, 
//and reduces virtaul address space gragmentation problem.
//Note: this class is thread-safe by passing true to the constructor function. However, you do not need to do this unless
//you are sure the memory allocation and de-allocation will happen in different threads. To make the pool thread safe
//increases allocation and deallocation cost.
//
class LL_COMMON_API LLPrivateMemoryPool
{
	friend class LLPrivateMemoryPoolManager ;

public:
	class LL_COMMON_API LLMemoryBlock //each block is devided into slots uniformly
	{
	public: 
		LLMemoryBlock() ;
		~LLMemoryBlock() ;

		void init(char* buffer, U32 buffer_size, U32 slot_size) ;
		void setBuffer(char* buffer, U32 buffer_size) ;

		char* allocate() ;
		void  freeMem(void* addr) ;

		bool empty() {return !mAllocatedSlots;}
		bool isFull() {return mAllocatedSlots == mTotalSlots;}
		bool isFree() {return !mTotalSlots;}

		U32  getSlotSize()const {return mSlotSize;}
		U32  getTotalSlots()const {return mTotalSlots;}
		U32  getBufferSize()const {return mBufferSize;}
		char* getBuffer() const {return mBuffer;}

		//debug use
		void resetBitMap() ;
	private:
		char* mBuffer;
		U32   mSlotSize ; //when the block is not initialized, it is the buffer size.
		U32   mBufferSize ;
		U32   mUsageBits ;
		U8    mTotalSlots ;
		U8    mAllocatedSlots ;
		U8    mDummySize ; //size of extra bytes reserved for mUsageBits.

	public:
		LLMemoryBlock* mPrev ;
		LLMemoryBlock* mNext ;
		LLMemoryBlock* mSelf ;

		struct CompareAddress
		{
			bool operator()(const LLMemoryBlock* const& lhs, const LLMemoryBlock* const& rhs)
			{
				return (size_t)lhs->getBuffer() < (size_t)rhs->getBuffer();
			}
		};
	};

	class LL_COMMON_API LLMemoryChunk //is divided into memory blocks.
	{
	public:
		LLMemoryChunk() ;
		~LLMemoryChunk() ;

		void init(char* buffer, U32 buffer_size, U32 min_slot_size, U32 max_slot_size, U32 min_block_size, U32 max_block_size) ;
		void setBuffer(char* buffer, U32 buffer_size) ;

		bool empty() ;
		
		char* allocate(U32 size) ;
		void  freeMem(void* addr) ;

		char* getBuffer() const {return mBuffer;}
		U32 getBufferSize() const {return mBufferSize;}
		U32 getAllocatedSize() const {return mAlloatedSize;}

		bool containsAddress(const char* addr) const;

		static U32 getMaxOverhead(U32 data_buffer_size, U32 min_slot_size, 
													   U32 max_slot_size, U32 min_block_size, U32 max_block_size) ;
	
		void dump() ;

	private:
		U32 getPageIndex(char const* addr) ;
		U32 getBlockLevel(U32 size) ;
		U16 getPageLevel(U32 size) ;
		LLMemoryBlock* addBlock(U32 blk_idx) ;
		void popAvailBlockList(U32 blk_idx) ;
		void addToFreeSpace(LLMemoryBlock* blk) ;
		void removeFromFreeSpace(LLMemoryBlock* blk) ;
		void removeBlock(LLMemoryBlock* blk) ;
		void addToAvailBlockList(LLMemoryBlock* blk) ;
		U32  calcBlockSize(U32 slot_size);
		LLMemoryBlock* createNewBlock(LLMemoryBlock* blk, U32 buffer_size, U32 slot_size, U32 blk_idx) ;

	private:
		LLMemoryBlock** mAvailBlockList ;//256 by mMinSlotSize
		LLMemoryBlock** mFreeSpaceList;
		LLMemoryBlock*  mBlocks ; //index of blocks by address.
		
		char* mBuffer ;
		U32   mBufferSize ;
		char* mDataBuffer ;
		char* mMetaBuffer ;
		U32   mMinBlockSize ;
		U32   mMinSlotSize ;
		U32   mMaxSlotSize ;
		U32   mAlloatedSize ;
		U16   mBlockLevels;
		U16   mPartitionLevels;

	public:
		//form a linked list
		LLMemoryChunk* mNext ;
		LLMemoryChunk* mPrev ;
	} ;

private:
	LLPrivateMemoryPool(S32 type, U32 max_pool_size) ;
	~LLPrivateMemoryPool() ;

	char *allocate(U32 size) ;
	void  freeMem(void* addr) ;
	
	void  dump() ;
	U32   getTotalAllocatedSize() ;
	U32   getTotalReservedSize() {return mReservedPoolSize;}
	S32   getType() const {return mType; }
	bool  isEmpty() const {return !mNumOfChunks; }

private:
	void lock() ;
	void unlock() ;	
	S32 getChunkIndex(U32 size) ;
	LLMemoryChunk*  addChunk(S32 chunk_index) ;
	bool checkSize(U32 asked_size) ;
	void removeChunk(LLMemoryChunk* chunk) ;
	U16  findHashKey(const char* addr);
	void addToHashTable(LLMemoryChunk* chunk) ;
	void removeFromHashTable(LLMemoryChunk* chunk) ;
	void rehash() ;
	bool fillHashTable(U16 start, U16 end, LLMemoryChunk* chunk) ;
	LLMemoryChunk* findChunk(const char* addr) ;

	void destroyPool() ;

public:
	enum
	{
		SMALL_ALLOCATION = 0, //from 8 bytes to 2KB(exclusive), page size 2KB, max chunk size is 4MB.
		MEDIUM_ALLOCATION,    //from 2KB to 512KB(exclusive), page size 32KB, max chunk size 4MB
		LARGE_ALLOCATION,     //from 512KB to 4MB(inclusive), page size 64KB, max chunk size 16MB
		SUPER_ALLOCATION      //allocation larger than 4MB.
	};

	enum
	{
		STATIC = 0 ,       //static pool(each alllocation stays for a long time) without threading support
		VOLATILE,          //Volatile pool(each allocation stays for a very short time) without threading support
		STATIC_THREADED,   //static pool with threading support
		VOLATILE_THREADED, //volatile pool with threading support
		MAX_TYPES
	}; //pool types

private:
	LLMutex* mMutexp ;
	U32  mMaxPoolSize;
	U32  mReservedPoolSize ;	

	LLMemoryChunk* mChunkList[SUPER_ALLOCATION] ; //all memory chunks reserved by this pool, sorted by address	
	U16 mNumOfChunks ;
	U16 mHashFactor ;

	S32 mType ;

	class LLChunkHashElement
	{
	public:
		LLChunkHashElement() {mFirst = NULL ; mSecond = NULL ;}

		bool add(LLMemoryChunk* chunk) ;
		void remove(LLMemoryChunk* chunk) ;
		LLMemoryChunk* findChunk(const char* addr) ;

		bool empty() {return !mFirst && !mSecond; }
		bool full()  {return mFirst && mSecond; }
		bool hasElement(LLMemoryChunk* chunk) {return mFirst == chunk || mSecond == chunk;}

	private:
		LLMemoryChunk* mFirst ;
		LLMemoryChunk* mSecond ;
	};
	std::vector<LLChunkHashElement> mChunkHashList ;
};

class LL_COMMON_API LLPrivateMemoryPoolManager
{
private:
	LLPrivateMemoryPoolManager(BOOL enabled, U32 max_pool_size) ;
	~LLPrivateMemoryPoolManager() ;

public:	
	static LLPrivateMemoryPoolManager* getInstance() ;
	static void initClass(BOOL enabled, U32 pool_size) ;
	static void destroyClass() ;

	LLPrivateMemoryPool* newPool(S32 type) ;
	void deletePool(LLPrivateMemoryPool* pool) ;

private:	
	std::vector<LLPrivateMemoryPool*> mPoolList ;	
	U32  mMaxPrivatePoolSize;

	static LLPrivateMemoryPoolManager* sInstance ;
	static BOOL sPrivatePoolEnabled;
	static std::vector<LLPrivateMemoryPool*> sDanglingPoolList ;
public:
	//debug and statistics info.
	void updateStatistics() ;

	U32 mTotalReservedSize ;
	U32 mTotalAllocatedSize ;

public:
#if __DEBUG_PRIVATE_MEM__
	static char* allocate(LLPrivateMemoryPool* poolp, U32 size, const char* function, const int line) ;	
	
	typedef std::map<char*, std::string> mem_allocation_info_t ;
	static mem_allocation_info_t sMemAllocationTracker;
#else
	static char* allocate(LLPrivateMemoryPool* poolp, U32 size) ;	
#endif
	static void  freeMem(LLPrivateMemoryPool* poolp, void* addr) ;
};

//-------------------------------------------------------------------------------------
#if __DEBUG_PRIVATE_MEM__
#define ALLOCATE_MEM(poolp, size) LLPrivateMemoryPoolManager::allocate((poolp), (size), __FUNCTION__, __LINE__)
#else
#define ALLOCATE_MEM(poolp, size) LLPrivateMemoryPoolManager::allocate((poolp), (size))
//#define ALLOCATE_MEM(poolp, size) new char[size]
#endif
#define FREE_MEM(poolp, addr) LLPrivateMemoryPoolManager::freeMem((poolp), (addr))
//#define FREE_MEM(poolp, addr) delete[] addr;
//-------------------------------------------------------------------------------------

//
//the below singleton is used to test the private memory pool.
//
#if 0
class LL_COMMON_API LLPrivateMemoryPoolTester
{
private:
	LLPrivateMemoryPoolTester() ;
	~LLPrivateMemoryPoolTester() ;

public:
	static LLPrivateMemoryPoolTester* getInstance() ;
	static void destroy() ;

	void run(S32 type) ;	

private:
	void correctnessTest() ;
	void performanceTest() ;
	void fragmentationtest() ;

	void test(U32 min_size, U32 max_size, U32 stride, U32 times, bool random_deletion, bool output_statistics) ;
	void testAndTime(U32 size, U32 times) ;

#if 0
public:
	void* operator new(size_t size)
	{
		return (void*)sPool->allocate(size) ;
	}
    void  operator delete(void* addr)
	{
		sPool->freeMem(addr) ;
	}
	void* operator new[](size_t size)
	{
		return (void*)sPool->allocate(size) ;
	}
    void  operator delete[](void* addr)
	{
		sPool->freeMem(addr) ;
	}
#endif

private:
	static LLPrivateMemoryPoolTester* sInstance;
	static LLPrivateMemoryPool* sPool ;
	static LLPrivateMemoryPool* sThreadedPool ;
};
#if 0
//static
void* LLPrivateMemoryPoolTester::operator new(size_t size)
{
	return (void*)sPool->allocate(size) ;
}

//static
void  LLPrivateMemoryPoolTester::operator delete(void* addr)
{
	sPool->free(addr) ;
}

//static
void* LLPrivateMemoryPoolTester::operator new[](size_t size)
{
	return (void*)sPool->allocate(size) ;
}

//static
void  LLPrivateMemoryPoolTester::operator delete[](void* addr)
{
	sPool->free(addr) ;
}
#endif
#endif

//EVENTUALLY REMOVE THESE:
#include "llpointer.h"
#include "llsingleton.h"
#include "llsafehandle.h"

#endif
