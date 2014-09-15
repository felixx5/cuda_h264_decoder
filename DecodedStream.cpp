//------------------------------------------------------------------------------
// File: DecodedStream.cpp
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
//
// Desc: The output pin class, handles delivery to downstream input pin.
//
//------------------------------------------------------------------------------

#include "DecodedStream.h"
#include "CudaDecodeInputPin.h"
#include "CudaDecodeFilter.h"
#include "SmartCache.h"
#include "CudaDecoder.h"
#include "MediaController.h"

DecodedStream::DecodedStream(TCHAR * inObjectName,
							 HRESULT * outResult, 
							 CudaDecodeFilter * inFilter) 
: CSourceStream(inObjectName, outResult, inFilter, L"Output")
{
	m_DecodeFilter = inFilter;
	m_Position     = NULL;
	m_Flushing     = FALSE;
	m_EOS_Flag	  = FALSE;
	m_MpegController = NULL;
	m_SamplesSent    = 0;
}

DecodedStream::~DecodedStream()
{
	if (m_Position) 
	{
		m_Position->Release();
		m_Position = NULL;
	}
}

void DecodedStream::SetController(MediaController* inController)
{
	m_MpegController = inController;
}

HRESULT DecodedStream::FillBuffer(IMediaSample *pSample)
{
	return NOERROR;
}

HRESULT DecodedStream::CompleteConnect(IPin *pReceivePin)
{
	HRESULT hr = m_DecodeFilter->CompleteConnect(PINDIR_OUTPUT, pReceivePin);
	if (FAILED(hr)) 
	{
		return hr;
	}
	return CBaseOutputPin::CompleteConnect(pReceivePin);
}

HRESULT DecodedStream::DecideBufferSize(IMemAllocator * pAllocator, ALLOCATOR_PROPERTIES *pprop)
{
	// Is the input pin connected
	if (!m_DecodeFilter->m_CudaDecodeInputPin->IsConnected()) 
	{
		return E_UNEXPECTED;
	}

	ASSERT(pAllocator);
	ASSERT(pprop);
	HRESULT hr = NOERROR;

	pprop->cbBuffer  = m_DecodeFilter->m_OutputImageSize;
	pprop->cBuffers  = 1;
	pprop->cbAlign   = 1;

	ASSERT(pprop->cbBuffer);

	// Ask the allocator to reserve us some sample memory, NOTE the function
	// can succeed (that is return NOERROR) but still not have allocated the
	// memory that we requested, so we must check we got whatever we wanted

	ALLOCATOR_PROPERTIES Actual;
	hr = pAllocator->SetProperties(pprop, &Actual);
	if (FAILED(hr)) 
	{
		return hr;
	}

	ASSERT( Actual.cBuffers == 1 );

	if (pprop->cBuffers > Actual.cBuffers || pprop->cbBuffer > Actual.cbBuffer) 
	{
			return E_FAIL;
	}
	return NOERROR;
}

HRESULT DecodedStream::CheckMediaType(const CMediaType *mtOut)
{
	if (m_DecodeFilter->m_CudaDecodeInputPin->IsConnected())
	{
		if ((mtOut->subtype == MEDIASUBTYPE_IYUV || mtOut->subtype == MEDIASUBTYPE_RGB24) 
			&& mtOut->formattype == FORMAT_VideoInfo)
		{
			VIDEOINFOHEADER * pFormat = (VIDEOINFOHEADER *) mtOut->pbFormat;
			if (pFormat->bmiHeader.biHeight == m_DecodeFilter->m_ImageHeight &&
				pFormat->bmiHeader.biWidth == m_DecodeFilter->m_ImageWidth)
			{
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT DecodedStream::GetMediaType(int iPosition, CMediaType *pMediaType)
{
	if (!m_DecodeFilter->m_CudaDecodeInputPin->IsConnected() || iPosition < 0 || iPosition > 1)
	{
		return E_FAIL;
	}

	VIDEOINFOHEADER    format;
	ZeroMemory(&format, sizeof(VIDEOINFOHEADER));
	pMediaType->SetType(&MEDIATYPE_Video);
	switch (iPosition)
	{
	case 0:  // YUY2
		pMediaType->SetSubtype(&MEDIASUBTYPE_IYUV);
		format.bmiHeader.biBitCount    = 16;
		format.bmiHeader.biCompression = mmioFOURCC('I','Y','U','V');
		break;

	case 1: // RGB24
		pMediaType->SetSubtype(&MEDIASUBTYPE_RGB24);
		format.bmiHeader.biBitCount    = 24;
		format.bmiHeader.biCompression = BI_RGB;
		break;
	}
	pMediaType->SetFormatType(&FORMAT_VideoInfo);
	format.bmiHeader.biSize   = sizeof(BITMAPINFOHEADER);
	format.bmiHeader.biPlanes = 1;
	format.AvgTimePerFrame    = m_DecodeFilter->m_SampleDuration;
	format.bmiHeader.biWidth  = m_DecodeFilter->m_ImageWidth;
	format.bmiHeader.biHeight = m_DecodeFilter->m_ImageHeight;
	format.bmiHeader.biSizeImage = m_DecodeFilter->m_ImageWidth * m_DecodeFilter->m_ImageHeight * format.bmiHeader.biBitCount / 8;
	pMediaType->SetFormat(PBYTE(&format), sizeof(VIDEOINFOHEADER));
	return S_OK;
} // GetMediaType

STDMETHODIMP DecodedStream::QueryId(LPWSTR * Id)
{
	return CBaseOutputPin::QueryId(Id);
}

// overriden to expose IMediaPosition and IMediaSeeking control interfaces
STDMETHODIMP DecodedStream::NonDelegatingQueryInterface(REFIID riid, void **ppv) 
{
	CheckPointer(ppv,E_POINTER);
	ValidateReadWritePtr(ppv,sizeof(PVOID));
	*ppv = NULL;

	if (riid == IID_IMediaPosition || riid == IID_IMediaSeeking) 
	{
		// we should have an input pin by now
		ASSERT(m_DecodeFilter->m_CudaDecodeInputPin != NULL);
		if (m_Position == NULL) 
		{
			HRESULT hr = CreatePosPassThru(GetOwner(),
				FALSE,
				(IPin *)m_DecodeFilter->m_CudaDecodeInputPin,
				&m_Position);
			if (FAILED(hr)) 
			{
				return hr;
			}
		}
		return m_Position->QueryInterface(riid, ppv);
	}
	else 
	{
		return CSourceStream::NonDelegatingQueryInterface(riid, ppv);
	}
}

STDMETHODIMP DecodedStream::Notify(IBaseFilter * pSender, Quality q) 
{
	UNREFERENCED_PARAMETER(pSender);
	ValidateReadPtr(pSender, sizeof(IBaseFilter));
	return m_DecodeFilter->m_CudaDecodeInputPin->PassNotify(q);
} // Notify

STDMETHODIMP DecodedStream::BeginFlush(void)
{
	m_Flushing = TRUE;
	m_MpegController->BeginFlush();
	{
		CAutoLock   lck(&m_DataAccess);
		m_SamplesSent   = 0;
	}
	return NOERROR;
}

STDMETHODIMP DecodedStream::EndFlush(void)
{
	m_MpegController->EndFlush();
	m_Flushing = FALSE;
	return NOERROR;
}

HRESULT DecodedStream::EndOfStream(void)
{
	return NOERROR;
}

HRESULT DecodedStream::DoBufferProcessingLoop(void)
{
	Command com;
	OnThreadStartPlay();

	m_EOS_Flag = FALSE;
	do 
	{
		while (!CheckRequest(&com)) 
		{
			// If no data, never enter blocking reading
			if (m_Flushing || m_MpegController->IsCacheEmpty() || m_EOS_Flag) 
			{
				if (m_DecodeFilter->m_EOSReceived)
				{
					m_EOS_Flag = TRUE;
					m_MpegController->EndEndOfStream();
					if (!m_DecodeFilter->m_EOSDelivered)
					{
						m_DecodeFilter->m_EOSDelivered = TRUE;
						DeliverEndOfStream();	
					}
				}

				Sleep(1);
				continue;
			}

			m_MpegController->m_SmartCache->ResetCacheChecking();

			BOOL pass = m_MpegController->DecodeOnePicture();
		}

		// For all commands sent to us there must be a Reply call!
		if (com == CMD_RUN || com == CMD_PAUSE) 
		{
			Reply(NOERROR);
		}
		else if (com != CMD_STOP) 
		{
			Reply((DWORD) E_UNEXPECTED);
			DbgLog((LOG_ERROR, 1, TEXT("Unexpected command!!!")));
		}
	} while (com != CMD_STOP);

	// If EOS not delivered at the moment, just do it here!
	//	mMpegController->EndEndOfStream();
	//	if (!mDecodeFilter->mEOSDelivered)
	//	{
	//		mDecodeFilter->mEOSDelivered = TRUE;
	//		DeliverEndOfStream();	
	//	}
	return S_FALSE;
}

HRESULT DecodedStream::OnThreadStartPlay(void)
{
	m_SamplesSent = 0;
	return NOERROR;
}

HRESULT DecodedStream::OnThreadDestroy(void)
{	
	return NOERROR;
}

HRESULT DecodedStream::DeliverCurrentPicture(IMediaSample * pSample)
{
	PBYTE   pOut;

	pSample->GetPointer(&pOut);
	m_MpegController->GetDecoded(pOut);
	pSample->SetActualDataLength(m_DecodeFilter->m_OutputImageSize);
	ULONG    alreadySent = 0;
	{
		CAutoLock   lck(&m_DataAccess);
		alreadySent  = m_SamplesSent;
		m_SamplesSent++;
	}
	LONGLONG   llStart = alreadySent;
	LONGLONG   llEnd   = alreadySent + 1;
	pSample->SetMediaTime(&llStart, &llEnd);
	REFERENCE_TIME	rtStart = alreadySent * m_DecodeFilter->m_SampleDuration;
	REFERENCE_TIME	rtEnd   = (alreadySent + 1) * m_DecodeFilter->m_SampleDuration;
	pSample->SetTime(&rtStart, &rtEnd);
	pSample->SetDiscontinuity(FALSE);
	pSample->SetPreroll(FALSE);
	pSample->SetSyncPoint(TRUE);
	
	HRESULT hr = Deliver(pSample);
	pSample->Release();
	return hr;
}

HRESULT DecodedStream::StopThreadSafely(void)
{
	if (ThreadExists()) 
	{
		Stop();
	}
	return NOERROR;
}

HRESULT DecodedStream::RunThreadSafely(void)
{
	if (ThreadExists()) 
	{
		Run();
	}
	return NOERROR;
}