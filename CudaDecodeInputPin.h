//------------------------------------------------------------------------------
// File: CudaDecodeInputPin.h
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
//
// Desc: The input pin class which handles connection with 
// upstream filter out pin and source frame receiving.
//
//------------------------------------------------------------------------------

#ifndef CUDA_DECODE_INPUT_PIN_H_
#define	CUDA_DECODE_INPUT_PIN_H_

#include "StdHeader.h"

class CudaDecodeFilter;

class CudaDecodeInputPin : public CBaseInputPin
{
	friend class CudaDecodeFilter;

public:

	CudaDecodeInputPin(	TCHAR* inObjectName, 
						CudaDecodeFilter* inFilter, 
						HRESULT* outResult);
	
	virtual ~CudaDecodeInputPin();

	virtual HRESULT CheckMediaType(const CMediaType * mtIn);
	STDMETHODIMP	Receive(IMediaSample *pSample);

	STDMETHODIMP	EndOfStream(void);
	STDMETHODIMP	BeginFlush(void);
	STDMETHODIMP	EndFlush(void);
	STDMETHODIMP	NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);

	HRESULT			CompleteConnect(IPin *pReceivePin);

	CMediaType&		CurrentMediaType(void) { return m_mt; }

private:

	CudaDecodeFilter* m_DecodeFilter;
};

#endif