//------------------------------------------------------------------------------
// File: MediaController.h
// 
// Author: Ren Yifei, Lin Ziya
//
// Contact: yfren@cs.hku.hk, zlin@cs.hku.hk
//
// Desc: The media controller class coordinates 
// the smart cache (frame buffer) and the decoder
//
//------------------------------------------------------------------------------

#ifndef MEDIA_CONTROLLER_H_
#define MEDIA_CONTROLLER_H_

#include "StdHeader.h"

class SmartCache;
class CudaH264Decoder;

class MediaController
{
	friend class DecodedStream;

public:
	MediaController();
	virtual ~MediaController();

public:
	bool Initialize(DecodedStream* outputPin);
	void Uninitialize(void);

	void SetOutputType(int inType);
	void SetOutputImageSize(long inImageSize);

	void BeginFlush(void);
	void EndFlush(void);
	void BeginEndOfStream(void);
	void EndEndOfStream(void);
	void FlushAllPending(void);

	bool ReceiveMpeg(unsigned char * inData, long inLength);
	void GetDecoded(unsigned char * outPicture);

	BOOL IsCacheInputWaiting(void);
	BOOL IsCacheOutputWaiting(void);
	BOOL IsCacheEmpty(void);

 	BOOL DecodeOnePicture(void);

private:

	long        m_OutputImageSize;

	int			m_StoreFlag;
	int			m_FaultFlag;
	BOOL		m_IsEOS;

	SmartCache* m_SmartCache;

	CudaH264Decoder* m_CudaH264Decoder;
};

#endif