//------------------------------------------------------------------------------
// File: CudaDecodeFilter.cpp
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
// 
// Desc: The main decoder class, manages the frame receive/deliver,
// derived from CSource in DirectShow.
//
//------------------------------------------------------------------------------

#include "CudaDecodeFilter.h"
#include "CudaDecodeInputPin.h"
#include "MediaController.h"
#include "DecodedStream.h"

const TCHAR* CUDA_DECODE_FILTER_NAME = L"CUDA H.264 Decoder";

static int decoderInstances = 0;

//Filter & pins info
const AMOVIESETUP_MEDIATYPE sudPinTypes =
{
	&MEDIATYPE_Video,       // Major type
	&MEDIASUBTYPE_NULL      // Minor type
};

const AMOVIESETUP_PIN psudPins[] =
{
	{
			L"Input",           // String pin name
			FALSE,              // Is it rendered
			FALSE,              // Is it an output
			FALSE,              // Allowed none
			FALSE,              // Allowed many
			&CLSID_NULL,        // Connects to filter
			L"Output",          // Connects to pin
			1,                  // Number of types
			&sudPinTypes		// The pin details
	},     
	{ 
			L"Output",          // String pin name
			FALSE,              // Is it rendered
			TRUE,               // Is it an output
			FALSE,              // Allowed none
			FALSE,              // Allowed many
			&CLSID_NULL,        // Connects to filter
			L"Input",           // Connects to pin
			1,                  // Number of types
			&sudPinTypes        // The pin details
	}
};

const AMOVIESETUP_FILTER sudCudaTransform =
{
	&CLSID_CudaDecodeFilter,			// Filter CLSID
	CUDA_DECODE_FILTER_NAME,			// Filter name
	MERIT_DO_NOT_USE,					// Its merit
	2,									// Number of pins
	psudPins							// Pin details
};


CFactoryTemplate g_Templates[1] = 
{
	{ CUDA_DECODE_FILTER_NAME
	, &CLSID_CudaDecodeFilter
	, CudaDecodeFilter::CreateInstance
	, NULL
	, &sudCudaTransform }
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

STDAPI DllRegisterServer()
{
	return AMovieDllRegisterServer2( TRUE );
}

STDAPI DllUnregisterServer()
{
	return AMovieDllRegisterServer2( FALSE );
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule, 
					  DWORD  dwReason, 
					  LPVOID lpReserved)
{
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}



CUnknown * WINAPI CudaDecodeFilter::CreateInstance( LPUNKNOWN punk, HRESULT *phr )
{
	CudaDecodeFilter* pFilter = new CudaDecodeFilter(NAME("CudaDecodeFilter"), punk, phr);
	if(pFilter == NULL)
	{
		*phr = E_OUTOFMEMORY;
	}

	return pFilter;
}

CudaDecodeFilter::CudaDecodeFilter( TCHAR *tszName, LPUNKNOWN punk, HRESULT *phr )
: CSource(tszName, punk, CLSID_CudaDecodeFilter)
{
	decoderInstances++;

	m_SampleDuration = 0;
	m_ImageWidth     = 0;
	m_ImageHeight    = 0;
	m_OutputImageSize = 0;

	m_IsFlushing    = FALSE;
	m_EOSDelivered  = FALSE;
	m_EOSReceived   = FALSE;
	m_CudaDecodeInputPin = NULL;

	*phr = NOERROR;

	DecodedStream * outStream = new DecodedStream(NAME("Output"), phr, this);

	m_MediaController = new MediaController();

	if (outStream == NULL)
	{
		*phr = E_OUTOFMEMORY;	
	}
	else
	{
		outStream->SetController(m_MediaController);
	}
}

CudaDecodeFilter::~CudaDecodeFilter()
{
	if (m_CudaDecodeInputPin)
	{
		delete m_CudaDecodeInputPin;
		m_CudaDecodeInputPin = NULL;
	}

	if(m_MediaController)
	{
		delete m_MediaController;
		m_MediaController = NULL;
	}

	decoderInstances--;
}

STDMETHODIMP CudaDecodeFilter::NonDelegatingQueryInterface( REFIID riid, void ** ppv )
{
	CheckPointer(ppv, E_POINTER);

	return CSource::NonDelegatingQueryInterface(riid, ppv);
}

int CudaDecodeFilter::GetPinCount()
{
	return 2;
}

CBasePin * CudaDecodeFilter::GetPin( int n )
{
	if (m_CudaDecodeInputPin == NULL)
	{
		HRESULT hr = NOERROR;
		m_CudaDecodeInputPin = new CudaDecodeInputPin(NAME("Input"), this, &hr);
		ASSERT(m_CudaDecodeInputPin);
	}

	switch (n)
	{
	case 0:
		return m_CudaDecodeInputPin;
	case 1:
		return m_paStreams[0];
	default:
		return NULL;
	}
}

STDMETHODIMP CudaDecodeFilter::FindPin( LPCWSTR Id, IPin ** ppPin )
{
	return CBaseFilter::FindPin(Id, ppPin);
}

STDMETHODIMP CudaDecodeFilter::Stop()
{
	CAutoLock lck1(&m_cStateLock);
	if (m_State == State_Stopped) 
	{
		return NOERROR;
	}

	// Succeed the Stop if we are not completely connected
	ASSERT(m_CudaDecodeInputPin == NULL || m_paStreams[0] != NULL);
	if (m_CudaDecodeInputPin == NULL || m_CudaDecodeInputPin->IsConnected() == FALSE ||
		m_paStreams[0]->IsConnected() == FALSE) 
	{
		m_State = State_Stopped;
		m_EOSDelivered = FALSE;
		return NOERROR;
	}

	// Important!!! Refuse to receive any more samples
	m_MediaController->FlushAllPending();
	// decommit the input pin before locking or we can deadlock
	m_CudaDecodeInputPin->Inactive();	

	// synchronize with Receive calls
	CAutoLock lck2(&m_csReceive);
	OutputPin()->BeginFlush();
	OutputPin()->Inactive();
	OutputPin()->EndFlush();

	// allow a class derived from CTransformFilter
	// to know about starting and stopping streaming
	HRESULT hr = StopStreaming();
	if (SUCCEEDED(hr)) 
	{
		// complete the state transition
		m_State = State_Stopped;
		m_EOSDelivered = FALSE;
	}
	return hr;
}

STDMETHODIMP CudaDecodeFilter::Pause()
{
	CAutoLock lck(&m_cStateLock);

	HRESULT hr = NOERROR;
	if (m_State == State_Paused) 
	{
		// (This space left deliberately blank)
	}

	// If we have no input pin or it isn't yet connected then when we are
	// asked to pause we deliver an end of stream to the downstream filter.
	// This makes sure that it doesn't sit there forever waiting for
	// samples which we cannot ever deliver without an input connection.
	else if (m_CudaDecodeInputPin == NULL || m_CudaDecodeInputPin->IsConnected() == FALSE) 
	{
		if(m_paStreams[0]->IsConnected() && m_EOSDelivered == FALSE) 
		{
			m_paStreams[0]->DeliverEndOfStream();
			m_EOSDelivered = TRUE;
		}
		m_State = State_Paused;
	}

	// We may have an input connection but no output connection
	// However, if we have an input pin we do have an output pin
	else if (m_paStreams[0]->IsConnected() == FALSE) 
	{
		m_State = State_Paused;
	}
	else 
	{
		if (m_State == State_Stopped) 
		{
			// allow a class derived from CTransformFilter
			// to know about starting and stopping streaming
			CAutoLock lck2(&m_csReceive);
			hr = StartStreaming();
		}
		if (SUCCEEDED(hr)) 
		{
			// Make sure the receive not blocking
			// Make sure the out-sending thread not working
			m_MediaController->FlushAllPending();

			hr = CBaseFilter::Pause();
		}
	}
	return hr;
}

HRESULT CudaDecodeFilter::StartStreaming()
{
	m_IsFlushing  = FALSE;
	m_EOSReceived = FALSE;
	return NOERROR;
}

HRESULT CudaDecodeFilter::StopStreaming()
{
	m_IsFlushing  = FALSE;
	m_EOSReceived = FALSE;
	return NOERROR;
}

HRESULT CudaDecodeFilter::Receive( IMediaSample *pSample )
{
	// Check for other streams and pass them on
	AM_SAMPLE2_PROPERTIES * const pProps = m_CudaDecodeInputPin->SampleProps();
	if (pProps->dwStreamId != AM_STREAM_MEDIA) 
	{
		return m_paStreams[0]->Deliver(pSample);
	}

	// Receive mpeg2 data to buffer
	ASSERT(pSample);
	long lSourceSize = pSample->GetActualDataLength();
	BYTE * pSourceBuffer;
	pSample->GetPointer(&pSourceBuffer);
	m_MediaController->ReceiveMpeg(pSourceBuffer, lSourceSize);
	return NOERROR;
}

HRESULT CudaDecodeFilter::EndOfStream( void )
{
	// Ignoring the more than twice EOS
	if (!m_EOSReceived)
	{
		m_EOSReceived  = TRUE;
		m_MediaController->BeginEndOfStream();
		// Wait for all caching data having been fetched out
		//	while (!mMpegController.IsCacheOutputWaiting() && 
		//		!mMpegController.IsCacheEmpty())
		//	{
		//		Sleep(10);
		//	}
		//	mMpegController.EndEndOfStream();

		//	mEOSDelivered = TRUE;
		//	return m_paStreams[0]->DeliverEndOfStream();
	}
	return NOERROR;
}

HRESULT CudaDecodeFilter::NewSegment( REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate )
{
	return m_paStreams[0]->DeliverNewSegment(tStart, tStop, dRate);
}

HRESULT CudaDecodeFilter::BeginFlush( void )
{
	HRESULT hr  = m_paStreams[0]->DeliverBeginFlush();
	m_IsFlushing = TRUE;
	OutputPin()->BeginFlush();
	return hr;
}

HRESULT CudaDecodeFilter::EndFlush( void )
{
	m_EOSReceived = FALSE;
	OutputPin()->EndFlush();
	m_IsFlushing = FALSE;
	return m_paStreams[0]->DeliverEndFlush();
}

HRESULT CudaDecodeFilter::CompleteConnect( PIN_DIRECTION inDirection, IPin * inReceivePin )
{
	if (inDirection == PINDIR_INPUT)
	{
		CMediaType  mtIn = m_CudaDecodeInputPin->CurrentMediaType();
		if (mtIn.formattype == FORMAT_VIDEOINFO2)
		{
			VIDEOINFOHEADER2 * pFormat = (VIDEOINFOHEADER2 *) mtIn.pbFormat;
			m_SampleDuration = pFormat->AvgTimePerFrame;
			m_ImageWidth     = pFormat->bmiHeader.biWidth;
			m_ImageHeight    = pFormat->bmiHeader.biHeight;

			// Init the decoder system
			m_MediaController->Uninitialize();
			m_MediaController->Initialize(this->OutputPin());
			return S_OK;
		}
	}
	else
	{
		CMediaType  mtOut = OutputPin()->CurrentMediaType();
		int bitcount = 2;
		if (mtOut.subtype == MEDIASUBTYPE_IYUV)
		{
			bitcount = 2;
			m_MediaController->SetOutputType(2);
		}
		else if (mtOut.subtype == MEDIASUBTYPE_RGB24)
		{
			bitcount = 3;
			m_MediaController->SetOutputType(1);
		}
		m_OutputImageSize = m_ImageWidth * m_ImageHeight * bitcount;
		m_MediaController->SetOutputImageSize(m_OutputImageSize);
		return S_OK;
	}
	return E_FAIL;
}