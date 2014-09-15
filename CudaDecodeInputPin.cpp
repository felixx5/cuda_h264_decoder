//------------------------------------------------------------------------------
// File: CudaDecodeInputPin.cpp
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
// 
// Desc: The input pin class which handles connection with 
// upstream filter out pin and source frame receiving.
//
//------------------------------------------------------------------------------

#include "CudaDecodeInputPin.h"
#include "CudaDecodeFilter.h"

CudaDecodeInputPin::CudaDecodeInputPin( TCHAR * inObjectName, 
										CudaDecodeFilter * inFilter, 
										HRESULT * outResult ) 
: CBaseInputPin(inObjectName, 
				inFilter, 
				inFilter->pStateLock(), 
				outResult, 
				L"Input")
{
	m_DecodeFilter = inFilter;
}

CudaDecodeInputPin::~CudaDecodeInputPin()
{

}

HRESULT CudaDecodeInputPin::CheckMediaType( const CMediaType * mtIn )
{
	if (mtIn->majortype == MEDIATYPE_Video && mtIn->subtype == MEDIATYPE_H264)
	{
		return NOERROR;
	}
	
	return E_FAIL;
}

STDMETHODIMP CudaDecodeInputPin::Receive( IMediaSample *pSample )
{
	CAutoLock lck(&m_DecodeFilter->m_csReceive);
	ASSERT(pSample);

	// check all is well with the base class
	HRESULT hr = CBaseInputPin::Receive(pSample);
	if (hr == S_OK) 
	{
		hr = m_DecodeFilter->Receive(pSample);
	}
	return hr;
}

STDMETHODIMP CudaDecodeInputPin::EndOfStream( void )
{
	CAutoLock  lck(&m_DecodeFilter->m_csReceive);
	HRESULT hr = CheckStreaming();
	if (hr == S_OK) 
	{
		hr = m_DecodeFilter->EndOfStream();
	}
	return hr;
}

STDMETHODIMP CudaDecodeInputPin::BeginFlush( void )
{
	CAutoLock lck(m_DecodeFilter->pStateLock());
	
	if (!IsConnected() || !m_DecodeFilter->m_paStreams[0]->IsConnected()) 
	{
		return VFW_E_NOT_CONNECTED;
	}

	HRESULT hr = CBaseInputPin::BeginFlush();
	if (FAILED(hr)) 
	{
		return hr;
	}
	return m_DecodeFilter->BeginFlush();
}

STDMETHODIMP CudaDecodeInputPin::EndFlush( void )
{
	CAutoLock lck(m_DecodeFilter->pStateLock());

	if (!IsConnected() || !m_DecodeFilter->m_paStreams[0]->IsConnected()) 
	{
		return VFW_E_NOT_CONNECTED;
	}

	HRESULT hr = m_DecodeFilter->EndFlush();
	if (FAILED(hr)) 
	{
		return hr;
	}
	return CBaseInputPin::EndFlush();
}

STDMETHODIMP CudaDecodeInputPin::NewSegment( REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate )
{
	CBasePin::NewSegment(tStart, tStop, dRate);
	return m_DecodeFilter->NewSegment(tStart, tStop, dRate);
}

HRESULT CudaDecodeInputPin::CompleteConnect( IPin *pReceivePin )
{
	HRESULT hr = m_DecodeFilter->CompleteConnect(PINDIR_INPUT, pReceivePin);
	if (FAILED(hr)) 
	{
		return hr;
	}
	return CBaseInputPin::CompleteConnect(pReceivePin);
}