//------------------------------------------------------------------------------
// File: DecodedStream.h
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
//
// Desc: The output pin class, handles delivery to downstream input pin.
//
//------------------------------------------------------------------------------

#ifndef DECODED_STREAM_H_
#define DECODED_STREAM_H_

#include "StdHeader.h"

class CudaDecodeFilter;
class MediaController;

class DecodedStream : public CSourceStream
{
	friend class CudaDecodeFilter;
	friend class CudaH264Decoder;

public:
	DecodedStream(TCHAR * inObjectName, 
				  HRESULT * outResult, 
				  CudaDecodeFilter * inFilter);

	virtual ~DecodedStream();

	void			SetController(MediaController * inController);

	// override to expose IMediaPosition
	STDMETHODIMP	NonDelegatingQueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP	BeginFlush(void);
	STDMETHODIMP	EndFlush(void);
	STDMETHODIMP	EndOfStream(void);

	HRESULT			StopThreadSafely(void);
	HRESULT			RunThreadSafely(void);

protected:

	virtual HRESULT FillBuffer(IMediaSample *pSample); // PURE
	virtual HRESULT DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pprop); // PURE

	virtual HRESULT CheckMediaType(const CMediaType *mtOut);
	virtual HRESULT GetMediaType(int iPosition, CMediaType *pMediaType);

	// IQualityControl
	STDMETHODIMP	Notify(IBaseFilter * pSender, Quality q);

	HRESULT			CompleteConnect(IPin *pReceivePin);
	STDMETHODIMP	QueryId(LPWSTR * Id);

	virtual HRESULT DoBufferProcessingLoop(void);
	virtual HRESULT OnThreadStartPlay(void);
	virtual HRESULT OnThreadDestroy(void);

	HRESULT			DeliverCurrentPicture(IMediaSample * pSample);

	// Media type
public:
	CMediaType&		CurrentMediaType(void) { return m_mt; }

private:
	CudaDecodeFilter*		m_DecodeFilter;
	MediaController*		m_MpegController;

	// implement IMediaPosition by passing upstream
	IUnknown*				m_Position;
	BOOL					m_Flushing;
	ULONG					m_SamplesSent;
	CCritSec				m_DataAccess;
	BOOL					m_EOS_Flag;
};

#endif