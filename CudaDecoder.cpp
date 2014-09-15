//------------------------------------------------------------------------------
// File: CudaDecoder.cpp
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
// 
// Desc: The decoder class based on CUDA Decoder API. 
// It handles actual frame decoding.
//
//------------------------------------------------------------------------------

#include "CudaDecoder.h"
#include "MediaController.h"
#include "CudaDecodeFilter.h"
#include "DecodedStream.h"
#include "CudaPostProcessing.h"

BYTE*			CudaH264Decoder::m_InputBuffer = NULL;
BYTE*			CudaH264Decoder::m_OutputYuv2Buffer = NULL;
DecodedStream*	CudaH264Decoder::m_DecodedStream = NULL;

CudaH264Decoder::CudaH264Decoder(unsigned int maxFrame) :	m_pD3D(NULL), m_pD3Dev(NULL), 
															m_cuContext(NULL), m_cuDevice(NULL), 
															m_cuInstanceCount(0), m_cuCtxLock(NULL)
{

}

CudaH264Decoder::~CudaH264Decoder()
{
	this->ReleaseCuda();

	if(m_OutputYuv2Buffer)
	{
		delete [] m_OutputYuv2Buffer;
		m_OutputYuv2Buffer = NULL;
	}

	if(m_InputBuffer)
	{
		delete [] m_InputBuffer;
		m_InputBuffer = NULL;
	}
}

bool CudaH264Decoder::Init(DecodedStream* decodedStream)
{
	memset(&m_parserInitParams, 0, sizeof(m_parserInitParams));
	memset(&m_state, 0, sizeof(m_state));

	if(!this->InitCuda(&m_state.cuCtxLock))
		return false;

	// init outputPin
	m_DecodedStream = decodedStream;

	// init InputBuffer
	m_InputBuffer = new BYTE[DECODER_BUFFER_SIZE];

	m_parserInitParams.CodecType = cudaVideoCodec_H264;
	m_parserInitParams.ulMaxNumDecodeSurfaces = MAX_FRM_CNT;
	m_parserInitParams.pUserData = &m_state;
	m_parserInitParams.pfnSequenceCallback	=	CudaH264Decoder::HandleVideoSequence;
	m_parserInitParams.pfnDecodePicture		=	CudaH264Decoder::HandlePictureDecode;
	m_parserInitParams.pfnDisplayPicture	=	CudaH264Decoder::HandlePictureDisplay;

	CUresult result;

	result = cuvidCreateVideoParser(&m_state.cuParser, &m_parserInitParams);

	if (result != CUDA_SUCCESS)
	{
		printf("Failed to create video parser (%d)\n", result);
		return false;
	}

	{
		CAutoCtxLock lck(m_state.cuCtxLock);
		result = cuStreamCreate(&m_state.cuStream, 0);
		if (result != CUDA_SUCCESS)
		{
			printf("cuStreamCreate failed (%d)\n", result);
			return false;
		}
	}

	// Init display queue
	for (int i=0; i<DISPLAY_DELAY; i++)
	{
		m_state.DisplayQueue[i].picture_index = -1;   // invalid
	}

	return true;
}


bool CudaH264Decoder::InitCuda(CUvideoctxlock *pLock)
{
	D3DPRESENT_PARAMETERS d3dpp;
	HRESULT hr;
	CUresult err;
	int lAdapter, lAdapterCount;

	if (m_cuInstanceCount != 0)
	{
		m_cuInstanceCount++;
		*pLock = m_cuCtxLock;
		return true;
	}

	err = cuInit(0);
	if (err != CUDA_SUCCESS)
	{
		return false;
	}
	// Create an instance of Direct3D.
	m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (m_pD3D == NULL)
	{
		return false;
	}

	lAdapterCount = m_pD3D->GetAdapterCount();
	for (lAdapter=0; lAdapter<lAdapterCount; lAdapter++)
	{
		// Create the Direct3D9 device and the swap chain. the swap 
		// chain is the same size as the current display mode. The format is RGB-32.
		ZeroMemory(&d3dpp, sizeof(d3dpp));
		d3dpp.Windowed = TRUE;
		d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
		d3dpp.BackBufferWidth = 640;
		d3dpp.BackBufferHeight = 480;
		d3dpp.BackBufferCount = 1;
		d3dpp.SwapEffect = D3DSWAPEFFECT_COPY;
		d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
		d3dpp.Flags = D3DPRESENTFLAG_VIDEO;//D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
		
		hr = m_pD3D->CreateDevice(	lAdapter,
									D3DDEVTYPE_HAL,
									GetDesktopWindow(),
									D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING,
									&d3dpp,
									&m_pD3Dev);

		if (hr == S_OK)
		{
			err = cuD3D9CtxCreate(&m_cuContext, &m_cuDevice, 0, m_pD3Dev);
			if (err == CUDA_SUCCESS)
			{
#if USE_FLOATING_CONTEXTS
				CUcontext curr_ctx = NULL;
				err = cuCtxPopCurrent(&curr_ctx); // Switch to a floating context
				if (err != CUDA_SUCCESS)
					printf("cuCtxPopCurrent: %d (g_cuContext=%p)\n", err, m_cuContext);
				err = cuvidCtxLockCreate(&m_cuCtxLock, m_cuContext);
				if (err != CUDA_SUCCESS)
					printf("cuvidCtxLockCreate: %d (g_cuContext=%p)\n", err, m_cuContext);
#endif
				*pLock = m_cuCtxLock;
				m_cuInstanceCount = 1;
				return true;
			}
			m_pD3Dev->Release();
			m_pD3Dev = NULL;
		}
	}
	return false;
}

bool CudaH264Decoder::ReleaseCuda()
{
	if (m_cuInstanceCount > 0)
	{
		if (--m_cuInstanceCount != 0)
		{
			return true;
		}
	}
#if USE_FLOATING_CONTEXTS
	if (m_cuCtxLock)
	{
		cuvidCtxLockDestroy(m_cuCtxLock);
		m_cuCtxLock = NULL;
	}
#endif
	if (m_cuContext)
	{
		CUresult err = cuCtxDestroy(m_cuContext);
		if (err != CUDA_SUCCESS)
			printf("WARNING: cuCtxDestroy failed (%d)\n", err);
		m_cuContext = NULL;
	}
	if (m_pD3Dev)
	{
		m_pD3Dev->Release();
		m_pD3Dev = NULL;
	}
	if (m_pD3D)
	{
		m_pD3D->Release();
		m_pD3D = NULL;
	}
	return true;
}

int CUDAAPI CudaH264Decoder::HandleVideoSequence(void *pvUserData, CUVIDEOFORMAT *pFormat)
{
	DecodeSession *state = (DecodeSession *)pvUserData;

	if ((pFormat->codec != state->dci.CodecType)
		|| (pFormat->coded_width != state->dci.ulWidth)
		|| (pFormat->coded_height != state->dci.ulHeight)
		|| (pFormat->chroma_format != state->dci.ChromaFormat))
	{
		CAutoCtxLock lck(state->cuCtxLock);
		if (state->cuDecoder)
		{
			cuvidDestroyDecoder(state->cuDecoder);
			state->cuDecoder = NULL;
		}
		memset(&state->dci, 0, sizeof(CUVIDDECODECREATEINFO));
		state->dci.ulWidth = pFormat->coded_width;
		state->dci.ulHeight = pFormat->coded_height;
		state->dci.ulNumDecodeSurfaces = MAX_FRM_CNT;
		state->dci.CodecType = pFormat->codec;
		state->dci.ChromaFormat = pFormat->chroma_format;
		// Output (pass through)
		state->dci.OutputFormat = cudaVideoSurfaceFormat_NV12;
		state->dci.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave; // No deinterlacing
		state->dci.ulTargetWidth = state->dci.ulWidth;
		state->dci.ulTargetHeight = state->dci.ulHeight;
		state->dci.ulNumOutputSurfaces = 1;
		state->dci.ulCreationFlags = cudaVideoCreate_PreferCUVID;

		// Create the decoder
		if (CUDA_SUCCESS != cuvidCreateDecoder(&state->cuDecoder, &state->dci))
		{
			printf("Failed to create video decoder\n");
			return 0;
		}

		//Init buffer
		m_OutputYuv2Buffer = new BYTE[pFormat->coded_width * pFormat->coded_height * 2];
	}
	return 1;
}


// Called by the video parser to decode a single picture
// Since the parser will deliver data as fast as it can, we need to make sure that the picture
// index we're attempting to use for decode is no longer used for display
int CUDAAPI CudaH264Decoder::HandlePictureDecode(void *pvUserData, CUVIDPICPARAMS *pPicParams)
{
	DecodeSession *state = (DecodeSession *)pvUserData;
	CAutoCtxLock lck(state->cuCtxLock);
	CUresult result;
	int flush_pos;

	if (pPicParams->CurrPicIdx < 0) // Should never happen
	{
		printf("Invalid picture index\n");
		return 0;
	}
	// Make sure that the new frame we're decoding into is not still in the display queue
	// (this could happen if we do not have enough free frame buffers to handle the max delay)
	flush_pos = state->display_pos; // oldest frame
	for (;;)
	{
		bool frame_in_use = false;
		for (int i=0; i<DISPLAY_DELAY; i++)
		{
			if (state->DisplayQueue[i].picture_index == pPicParams->CurrPicIdx)
			{
				frame_in_use = true;
				break;
			}
		}
		if (!frame_in_use)
		{
			// No problem: we're safe to use this frame
			break;
		}
		// The target frame is still pending in the display queue:
		// Flush the oldest entry from the display queue and repeat
		if (state->DisplayQueue[flush_pos].picture_index >= 0)
		{
			//DisplayPicture(state, &state->DisplayQueue[flush_pos]);

			CudaH264Decoder::PostProcessing(state, &state->DisplayQueue[state->display_pos]);

			CudaH264Decoder::SendFrameDownStream();

			state->pic_cnt++;

			state->DisplayQueue[flush_pos].picture_index = -1;
		}
		flush_pos = (flush_pos + 1) % DISPLAY_DELAY;
	}
	result = cuvidDecodePicture(state->cuDecoder, pPicParams);
	
	if (result != CUDA_SUCCESS)
	{
		printf("cuvidDecodePicture: %d\n", result);
	}

	return (result == CUDA_SUCCESS);
}

// Called by the video parser to display a video frame (in the case of field pictures, there may be
// 2 decode calls per 1 display call, since two fields make up one frame)
int CUDAAPI CudaH264Decoder::HandlePictureDisplay(void *pvUserData, CUVIDPARSERDISPINFO *pPicParams)
{
	DecodeSession *state = (DecodeSession *)pvUserData;

	if (state->DisplayQueue[state->display_pos].picture_index >= 0)
	{

		CudaH264Decoder::PostProcessing(state, &state->DisplayQueue[state->display_pos]);
		
		CudaH264Decoder::SendFrameDownStream();

		state->pic_cnt++;
		
		state->DisplayQueue[state->display_pos].picture_index = -1;
	}

	state->DisplayQueue[state->display_pos] = *pPicParams;
	state->display_pos = (state->display_pos + 1) % DISPLAY_DELAY;
	
	return TRUE;
}


bool CudaH264Decoder::FetchVideoData( BYTE* ptr, unsigned int size )
{
	CUVIDSOURCEDATAPACKET pkt;

	if (size <= 0)
	{
		// Flush the decoder
		pkt.flags = CUVID_PKT_ENDOFSTREAM;
		pkt.payload_size = 0;
		pkt.payload = NULL;
		pkt.timestamp = 0;
		cuvidParseVideoData(m_state.cuParser, &pkt);
		return false;
	}

	pkt.flags = 0;
	pkt.payload_size = size;
	pkt.payload = ptr;
	pkt.timestamp = 0;  // not using timestamps
	cuvidParseVideoData(m_state.cuParser, &pkt);

	return true;
}


BYTE* CudaH264Decoder::GetOutputBufferPtr() const
{
	return m_OutputYuv2Buffer;
}

void CudaH264Decoder::SendFrameDownStream()
{
 	IMediaSample *pSample;
  	HRESULT hr = m_DecodedStream->GetDeliveryBuffer(&pSample, NULL, NULL, 0);
  	if (FAILED(hr)) 
  	{
  		Sleep(1);
		return;
  	}
  	hr = m_DecodedStream->DeliverCurrentPicture(pSample);
  	if (FAILED(hr) && m_DecodedStream->m_DecodeFilter->m_EOSReceived)
  	{
  		m_DecodedStream->m_EOS_Flag = TRUE; // testing!
  		m_DecodedStream->m_MpegController->EndEndOfStream();
  		if (!m_DecodedStream->m_DecodeFilter->m_EOSDelivered)
  		{
  			m_DecodedStream->m_DecodeFilter->m_EOSDelivered = TRUE;
  			m_DecodedStream->DeliverEndOfStream();	
  		}
  	}
}

int CudaH264Decoder::PostProcessing( DecodeSession *state, CUVIDPARSERDISPINFO *pPicParams)
{
	CAutoCtxLock lck(state->cuCtxLock);
	CUVIDPROCPARAMS vpp;
	CUdeviceptr devPtr;
	CUresult result;
	unsigned int pitch = 0, w, h;
	int nv12_size;

	memset(&vpp, 0, sizeof(vpp));
	vpp.progressive_frame = pPicParams->progressive_frame;
	vpp.top_field_first = pPicParams->top_field_first;
	result = cuvidMapVideoFrame(state->cuDecoder, pPicParams->picture_index, &devPtr, &pitch, &vpp);
	if (result != CUDA_SUCCESS)
	{
		printf("cuvidMapVideoFrame: %d\n", result);
		return 0;
	}
	w = state->dci.ulTargetWidth;
	h = state->dci.ulTargetHeight;
	nv12_size = pitch * (h + h/2);  // 12bpp
	if ((!state->pRawNV12) || (nv12_size > state->raw_nv12_size))
	{
		state->raw_nv12_size = 0;
		if (state->pRawNV12)
		{
			cuMemFreeHost(state->pRawNV12);    // Just to be safe (the pitch should be constant)
			state->pRawNV12 = NULL;
		}
		result = cuMemAllocHost((void**)&state->pRawNV12, nv12_size);
		if (result != CUDA_SUCCESS)
			printf("cuMemAllocHost failed to allocate %d bytes (%d)\n", nv12_size, result);
		state->raw_nv12_size = nv12_size;
	}
	if (state->pRawNV12)
	{
#if USE_ASYNC_COPY
		result = cuMemcpyDtoHAsync(state->pRawNV12, devPtr, nv12_size, state->cuStream);
		if (result != CUDA_SUCCESS)
			printf("cuMemcpyDtoHAsync: %d\n", result);
		// Gracefully wait for async copy to complete
		while (CUDA_ERROR_NOT_READY == cuStreamQuery(state->cuStream))
		{
			Sleep(1);
		}
#else
		result = cuMemcpyDtoH(state->pRawNV12, devPtr, nv12_size);
#endif
	}

	// Convert the output to standard IYUV and dump it to disk (note: very inefficient)
	if (state->pRawNV12)
	{
		unsigned int y;

		const unsigned char *p = state->pRawNV12;
		unsigned char *iyuv = m_OutputYuv2Buffer;//= new unsigned char [w*h+w*(h>>1)];

		// Copy luma
		for (y=0; y<h; y++)
		{
			memcpy(iyuv+y*w, p+y*pitch, w);
		}
		// De-interleave chroma (NV12 stored as U,V,U,V,...)
		p += h*pitch;
		for (y=0; y<h/2; y++)
		{
			for (unsigned int x=0; x<w/2; x++)
			{
				iyuv[w*h+y*w/2+x] = p[y*pitch+x*2];
				iyuv[w*h+(h/2)*(w/2)+y*w/2+x] = p[y*pitch+x*2+1];
			}
		}
		//////////////////////////////////////////////////////////////////////////

		//FILE* outfile = fopen("outtest.yuv", "a");
		//fwrite(iyuv, 1, w*h+w*(h/2), outfile);
		//fclose(outfile);

		//////////////////////////////////////////////////////////////////////////

		//fwrite(iyuv, 1, w*h+w*(h/2), state->fd_yuv);
		//this->copyToBuffer(iyuv, w*h+w*(h/2));
		// 		int size = w*h+w*(h/2);
		// 
		// 		if(!CudaH264Decoder::copyToBuffer(iyuv, size))
		// 			return 0;

		//++m_FrameNumInBuffer;

		//delete iyuv;



		//float hueColorSpaceMat[9];

		// TODO: Stage for handling video post processing

		// Final Stage: NV12toARGB color space conversion
		//SetColorSpaceMatrix (ITU601, hueColorSpaceMat);

		//UpdateConstantMemory(hueColorSpaceMat);
		
		//CUresult eResult;

// 		eResult = CudaNV12ToARGB(	*ppDecodedFrame, nDecodedPitch,
// 									*ppTextureData,  nTexturePitch,
// 									w, h, fpCudaKernel, streamID);

	}

	cuvidUnmapVideoFrame(state->cuDecoder, devPtr);

	return 1;
}