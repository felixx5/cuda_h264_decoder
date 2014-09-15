//------------------------------------------------------------------------------
// File: CudaDecodeFilter.h
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
//
// Desc: The main decoder class, manages the frame receive/deliver,
// derived from CSource in DirectShow.
//
//------------------------------------------------------------------------------

#ifndef CUDA_DECODE_FILTER_H_ 
#define CUDA_DECODE_FILTER_H_

#include "StdHeader.h"

class CudaDecodeInputPin;
class DecodedStream;
class MediaController;

class CudaDecodeFilter : public CSource
{
	friend class CudaDecodeInputPin;
	friend class DecodedStream;
	friend class CudaH264Decoder;

public:

	static CUnknown * WINAPI CreateInstance(LPUNKNOWN punk, HRESULT *phr);

private:

	CudaDecodeFilter(TCHAR *tszName, LPUNKNOWN punk, HRESULT *phr);

	virtual ~CudaDecodeFilter();

public:

	DECLARE_IUNKNOWN;
	
	STDMETHODIMP		NonDelegatingQueryInterface(REFIID riid, void ** ppv);

	virtual int			GetPinCount();
	virtual CBasePin*	GetPin(int n);
	STDMETHODIMP		FindPin(LPCWSTR Id, IPin ** ppPin);

	STDMETHODIMP		Stop();
	STDMETHODIMP		Pause();
	HRESULT				StartStreaming();
	HRESULT				StopStreaming();

	// Input pin's delegating methods
	HRESULT				Receive(IMediaSample *pSample);

	HRESULT				EndOfStream(void);
	HRESULT				BeginFlush(void);
	HRESULT				EndFlush(void);
	HRESULT				NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);

	// Output pin's delegating methods
	HRESULT				CompleteConnect(PIN_DIRECTION inDirection, IPin * inReceivePin);

private:

	DecodedStream *		OutputPin() {return (DecodedStream*) m_paStreams[0];};

private:

	CudaDecodeInputPin*		m_CudaDecodeInputPin;
	MediaController*		m_MediaController;
	CCritSec				m_csReceive;
	
	BOOL					m_IsFlushing;
	BOOL					m_EOSDelivered;
	BOOL					m_EOSReceived;

	// Bitmap information
	LONG					m_ImageWidth;
	LONG					m_ImageHeight;
	LONG					m_OutputImageSize;
	REFERENCE_TIME			m_SampleDuration;
};


#endif