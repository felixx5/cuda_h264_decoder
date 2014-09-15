//------------------------------------------------------------------------------
// File: MediaController.cpp
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
//
// Desc: The media controller class coordinates 
// the smart cache (frame buffer) and the decoder
//
//------------------------------------------------------------------------------

#include "MediaController.h"
#include "SmartCache.h"
#include "CudaDecoder.h"

MediaController::MediaController() :	m_FaultFlag(0), 
										m_IsEOS(0),
										m_StoreFlag(0), 
										m_CudaH264Decoder(NULL), 
										m_SmartCache(NULL), 
										m_OutputImageSize(0)
{

}

MediaController::~MediaController()
{

}

bool MediaController::Initialize( DecodedStream* outputPin )
{
	// testing
	m_SmartCache = new SmartCache();

	m_CudaH264Decoder = new CudaH264Decoder();
	m_CudaH264Decoder->Init(outputPin);

	return m_SmartCache != NULL && m_CudaH264Decoder != NULL;
}

void MediaController::Uninitialize( void )
{
	if(m_CudaH264Decoder)
	{
		delete m_CudaH264Decoder;
		m_CudaH264Decoder = NULL;
	}

	if(m_SmartCache)
	{
		delete m_SmartCache;
		m_SmartCache = NULL;
	}
}

void MediaController::SetOutputType( int inType )
{
	m_StoreFlag = inType;
}

void MediaController::SetOutputImageSize( long inImageSize )
{
	m_OutputImageSize = inImageSize;
}

void MediaController::BeginFlush( void )
{
	m_FaultFlag = ERROR_FLUSH;   // Give a chance to exit decoding cycle.
	m_SmartCache->BeginFlush();
	Sleep(10);
}

void MediaController::EndFlush( void )
{
	m_FaultFlag = 0;
	m_SmartCache->EndFlush();
}

void MediaController::BeginEndOfStream( void )
{
	m_IsEOS = TRUE;
	if (m_SmartCache->CheckOutputWaiting())
	{
		m_FaultFlag = ERROR_FLUSH;
		m_SmartCache->BeginFlush();
		Sleep(10);
		m_SmartCache->EndFlush();
	}
}

void MediaController::EndEndOfStream( void )
{
	m_IsEOS = FALSE;
}

void MediaController::FlushAllPending( void )
{
	m_FaultFlag = ERROR_FLUSH;
	m_SmartCache->BeginFlush();
	Sleep(10);
	m_SmartCache->EndFlush();
	m_FaultFlag = 0;
}

bool MediaController::ReceiveMpeg( unsigned char * inData, long inLength )
{
	long pass = m_SmartCache->Receive(inData, inLength);
	return pass > 0 ? true : false;
}

void MediaController::GetDecoded( unsigned char * outPicture )
{
	//	FinalDecodedOut(outPicture);
	if (m_StoreFlag == STORE_IYUY)
	{
		memcpy(outPicture, m_CudaH264Decoder->GetOutputBufferPtr(), m_OutputImageSize);
	}
	else
	{
		memcpy(outPicture, m_CudaH264Decoder->GetOutputBufferPtr(), m_OutputImageSize);
	}
}

BOOL MediaController::IsCacheInputWaiting( void )
{
	return m_SmartCache->CheckInputWaiting();
}

BOOL MediaController::IsCacheOutputWaiting( void )
{
	return m_SmartCache->CheckOutputWaiting();
}

BOOL MediaController::IsCacheEmpty( void )
{
	return m_SmartCache->GetAvailable() > 0 ? FALSE : TRUE;
}

BOOL MediaController::DecodeOnePicture( void )
{
	long available = m_SmartCache->GetAvailable();
	
	long readSize = DECODER_BUFFER_SIZE;

	if(available == 0)
	{
		m_FaultFlag = ERROR_FLUSH;
		return FALSE;
	}
	
	if(available < DECODER_BUFFER_SIZE)
		readSize = available;

	if(m_SmartCache->FetchData(m_CudaH264Decoder->m_InputBuffer, readSize) == 0)
	{
		m_FaultFlag = ERROR_FLUSH;//testing !!!
		return FALSE;
	}

	if(!m_CudaH264Decoder->FetchVideoData(m_CudaH264Decoder->m_InputBuffer, readSize))
		return FALSE;

	return TRUE;
}