//------------------------------------------------------------------------------
// File: SmartCache.cpp
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
//
// Desc: a automatic cache which works as a frame buffer. 
// It is single  reader/writer mode and should be 
// synchronized when reading or writing.
//
//------------------------------------------------------------------------------

#include "SmartCache.h"

long SmartCache::Init(void)
{
	m_InputCache = (unsigned char *)malloc(m_CacheSize);
	m_ReadingOffset = 0;
	m_WritingOffset = 0;
	m_InputWaiting  = FALSE;
	m_OutputWaiting = FALSE;
	m_CacheChecking = TRUE;  // When checking, maybe return to the cache header
	// Critical section to access smart cache
	InitializeCriticalSection(&singleAccess); 
	return (m_InputCache != NULL);
}

void SmartCache::Release(void)
{
	DeleteCriticalSection(&singleAccess);
	if (m_InputCache)
	{
		free(m_InputCache);
		m_InputCache = NULL;
	}
}

// Blocking receive
long SmartCache::Receive(unsigned char * inData, long inLength)
{	
	while (!m_IsFlushing && !HasEnoughSpace(inLength))
	{
		m_InputWaiting = TRUE;
		MakeSpace();
		Sleep(2);
	}
	m_InputWaiting = FALSE;

	if (!m_IsFlushing && HasEnoughSpace(inLength))
	{
		EnterCriticalSection(&singleAccess); // Enter
		memcpy(m_InputCache + m_WritingOffset, inData, inLength);
		m_WritingOffset += inLength;
		LeaveCriticalSection(&singleAccess); // Leave
		return 1;
	}
	return 0;
}

// Blocking read
long SmartCache::FetchData(BYTE * outBuffer, ULONG inLength)
{
	if (inLength <= 0)
		return 0;

	while (!m_IsFlushing && !HasEnoughData(inLength))
	{
		m_OutputWaiting = TRUE;
		Sleep(1);
	}
	m_OutputWaiting = FALSE;

	if (!m_IsFlushing && HasEnoughData(inLength))
	{
		EnterCriticalSection(&singleAccess); // Enter
		memcpy(outBuffer, m_InputCache + m_ReadingOffset, inLength);
		m_ReadingOffset += inLength;
		LeaveCriticalSection(&singleAccess); // Leave
		return inLength;
	}
	return 0;
}

long SmartCache::GetAvailable(void)
{
	return (m_WritingOffset - m_ReadingOffset);
}

// Determine whether enough space to hold coming mpeg data
long SmartCache::HasEnoughSpace(long inNeedSize)
{
	return (inNeedSize <= m_CacheSize - m_WritingOffset);
}

// Determine data in smart cache at this moment is enough to decode
long SmartCache::HasEnoughData(long inNeedSize)
{
	return (inNeedSize <= m_WritingOffset - m_ReadingOffset);
}

void SmartCache::MakeSpace(void)
{
	long workingSize = m_WritingOffset - m_ReadingOffset;
	// When cache checking, don't drop any data
	if (!m_CacheChecking && workingSize < m_MinWorkSize)
	{
		EnterCriticalSection(&singleAccess); // Enter
		memmove(m_InputCache, m_InputCache + m_ReadingOffset, workingSize);
		m_ReadingOffset = 0;
		m_WritingOffset = workingSize;
		LeaveCriticalSection(&singleAccess); // Leave
	}
}

void SmartCache::BeginFlush(void)
{
	m_IsFlushing = TRUE;
	m_WaitingCounter = 0;
	while (m_InputWaiting && m_WaitingCounter < 15)  // Make sure NOT block in receiving or reading
	{
		m_WaitingCounter++;
		Sleep(1);
	}
	m_WaitingCounter = 0;
	while (m_OutputWaiting && m_WaitingCounter < 15)
	{
		m_WaitingCounter++;
		Sleep(1);
	}
	//	Sleep(10);
	EnterCriticalSection(&singleAccess); // Enter
	m_ReadingOffset = 0;
	m_WritingOffset = 0;
	LeaveCriticalSection(&singleAccess); // Leave
}

void SmartCache::EndFlush(void)
{
	m_IsFlushing = FALSE;
}

BOOL SmartCache::CheckInputWaiting(void)
{
	return m_InputWaiting;
}

BOOL SmartCache::CheckOutputWaiting(void)
{
	return m_OutputWaiting;
}

// We can reuse the data having been read out
void SmartCache::SetCacheChecking(void)
{
	m_CacheChecking = TRUE;
}

void SmartCache::ResetCacheChecking(void)
{
	m_CacheChecking = FALSE;
	//EnterCriticalSection(&singleAccess); // Enter
	//gReadingOffset = 0; // testing!!
	//LeaveCriticalSection(&singleAccess); // Leave
}

SmartCache::SmartCache() : 	m_InputCache(NULL), 
							m_CacheSize(SMART_CACHE_SIZE), 
							m_MinWorkSize(MIN_WORK_SIZE), 
							m_ReadingOffset(0), 
							m_WritingOffset(0), 
							m_IsFlushing(FALSE),
							m_InputWaiting(FALSE),
							m_OutputWaiting(FALSE),
							m_CacheChecking(TRUE),
							m_WaitingCounter(0)
{
	this->Init();
}

SmartCache::~SmartCache()
{
	this->Release();
}