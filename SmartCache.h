//------------------------------------------------------------------------------
// File: SmartCache.h
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

#ifndef SMART_CACHE_H_
#define SMART_CACHE_H_

#include "StdHeader.h"

class SmartCache
{
public:

	SmartCache();
	virtual ~SmartCache();

	long Init(void);
	void Release(void);

	long Receive(unsigned char * inData, long inLength);
	long FetchData(BYTE * outBuffer, ULONG inLength);

	void BeginFlush(void);
	void EndFlush(void);
	long GetAvailable(void);

	BOOL CheckInputWaiting(void);
	BOOL CheckOutputWaiting(void);

	void SetCacheChecking(void);
	void ResetCacheChecking(void);

protected:

	void MakeSpace(void);
	long HasEnoughSpace(long inNeedSize);
	long HasEnoughData(long inNeedSize);

private:

	CRITICAL_SECTION singleAccess;

	unsigned char* m_InputCache ;
	long m_CacheSize;
	long m_MinWorkSize;
	long m_ReadingOffset;
	long m_WritingOffset;
	BOOL m_IsFlushing;

	BOOL m_InputWaiting;
	BOOL m_OutputWaiting;
	BOOL m_CacheChecking;
	int  m_WaitingCounter;
};


#endif