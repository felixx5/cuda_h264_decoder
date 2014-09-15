/*
NV12ToARGB color space conversion CUDA kernel

This sample uses CUDA to perform a simple NV12 (YUV 4:2:0 planar) 
source and converts to output in ARGB format
*/

#include "CudaPostProcessing.h"

__constant__ uint32 constAlpha;

#define MUL(x,y)    (x*y)

__constant__ float  constHueColorSpaceMat[9];


extern "C"
CUresult  UpdateConstantMemory(float *hueCSC)
{
	CUdeviceptr d_constHueCSC, d_constAlpha;
	unsigned int d_cscBytes, d_alphaBytes;

	// First grab the global device pointers from the CUBIN
	//cuModuleGetGlobal(&d_constHueCSC,  &d_cscBytes  , module, "constHueColorSpaceMat");
	//cuModuleGetGlobal(&d_constAlpha ,  &d_alphaBytes, module, "constAlpha"           );

	CUresult error = CUDA_SUCCESS;

	// Copy the constants to video memory
	cuMemcpyHtoD( d_constHueCSC, reinterpret_cast<const void *>(hueCSC), d_cscBytes);
	
	cutilDrvCheckMsg("cuMemcpyHtoD (d_constHueCSC) copy to Constant Memory failed");

	uint32 cudaAlpha = ((uint32)0xff<< 24);

	cuMemcpyHtoD( constAlpha, reinterpret_cast<const void *>(&cudaAlpha), d_alphaBytes);
	
	cutilDrvCheckMsg("cuMemcpyHtoD (constAlpha) copy to Constant Memory failed");

	return error;
}

extern "C"
void SetColorSpaceMatrix(eColorSpace CSC, float *hueCSC)
{
	float hueSin = 0.0f;
	float hueCos = 1.0f;

	//optimize !!!
	if (CSC == ITU601) {
		//CCIR 601
		hueCSC[0] = 1.1644f;
		hueCSC[1] = hueSin * 1.5960f;
		hueCSC[2] = hueCos * 1.5960f;
		hueCSC[3] = 1.1644f;
		hueCSC[4] = (hueCos * -0.3918f) - (hueSin * 0.8130f);
		hueCSC[5] = (hueSin *  0.3918f) - (hueCos * 0.8130f);  
		hueCSC[6] = 1.1644f;
		hueCSC[7] = hueCos *  2.0172f;
		hueCSC[8] = hueSin * -2.0172f;
	} else if (CSC == ITU709) {
		//CCIR 709
		hueCSC[0] = 1.0f;
		hueCSC[1] = hueSin * 1.57480f;
		hueCSC[2] = hueCos * 1.57480f;
		hueCSC[3] = 1.0;
		hueCSC[4] = (hueCos * -0.18732f) - (hueSin * 0.46812f);
		hueCSC[5] = (hueSin *  0.18732f) - (hueCos * 0.46812f);  
		hueCSC[6] = 1.0f;
		hueCSC[7] = hueCos *  1.85560f;
		hueCSC[8] = hueSin * -1.85560f;
	}
}



__device__ void YUV2RGB(uint32 *yuvi, float *red, float *green, float *blue)
{
	float luma, chromaCb, chromaCr;

	// Prepare for hue adjustment
	luma     = (float)yuvi[0];
	chromaCb = (float)((int32)yuvi[1] - 512.0f);
	chromaCr = (float)((int32)yuvi[2] - 512.0f);

	// Convert YUV To RGB with hue adjustment
	*red  = MUL(luma,     constHueColorSpaceMat[0]) + 
		MUL(chromaCb, constHueColorSpaceMat[1]) + 
		MUL(chromaCr, constHueColorSpaceMat[2]);
	*green= MUL(luma,     constHueColorSpaceMat[3]) + 
		MUL(chromaCb, constHueColorSpaceMat[4]) + 
		MUL(chromaCr, constHueColorSpaceMat[5]);
	*blue = MUL(luma,     constHueColorSpaceMat[6]) + 
		MUL(chromaCb, constHueColorSpaceMat[7]) + 
		MUL(chromaCr, constHueColorSpaceMat[8]);
}


__device__ uint32 RGBAPACK_8bit(float red, float green, float blue, uint32 alpha)
{
	uint32 ARGBpixel = 0;

	// Clamp final 10 bit results
	red   = min(max(red,   0.0f), 255.0f);
	green = min(max(green, 0.0f), 255.0f);
	blue  = min(max(blue,  0.0f), 255.0f);

	// Convert to 8 bit unsigned integers per color component
	ARGBpixel = (((uint32)blue ) | 
		(((uint32)green) << 8)  | 
		(((uint32)red  ) << 16) | (uint32)alpha);

	return  ARGBpixel;
}

__device__ uint32 RGBAPACK_10bit(float red, float green, float blue, uint32 alpha)
{
	uint32 ARGBpixel = 0;

	// Clamp final 10 bit results
	red   = min(max(red,   0.0f), 1023.f);
	green = min(max(green, 0.0f), 1023.f);
	blue  = min(max(blue,  0.0f), 1023.f);

	// Convert to 8 bit unsigned integers per color component
	ARGBpixel = (((uint32)blue  >> 2) | 
		(((uint32)green >> 2) << 8)  | 
		(((uint32)red   >> 2) << 16) | (uint32)alpha);

	return  ARGBpixel;
}


// CUDA kernel for outputing the final ARGB output from NV12;
extern "C"
__global__ void Passthru_drvapi(uint32 *srcImage,	uint32 nSourcePitch, 
								uint32 *dstImage,	uint32 nDestPitch,
								uint32 width,		uint32 height)
{
	int32 x, y;
	uint32 yuv101010Pel[2];
	uint32 processingPitch = ((width) + 63) & ~63;
	uint32 dstImagePitch   = nDestPitch >> 2;
	uint8 *srcImageU8     = (uint8 *)srcImage;

	processingPitch = nSourcePitch;

	// Pad borders with duplicate pixels, and we multiply by 2 because we process 2 pixels per thread
	x = blockIdx.x * (blockDim.x << 1) + (threadIdx.x << 1);
	y = blockIdx.y *  blockDim.y       +  threadIdx.y;

	if (x >= width)
		return; //x = width - 1;
	if (y >= height)
		return; // y = height - 1;

	// Read 2 Luma components at a time, so we don't waste processing since CbCr are decimated this way.
	// if we move to texture we could read 4 luminance values
	yuv101010Pel[0] = (srcImageU8[y * processingPitch + x    ]);
	yuv101010Pel[1] = (srcImageU8[y * processingPitch + x + 1]);

	// this steps performs the color conversion
	float luma[2];

	luma[0]   =  (yuv101010Pel[0]        & 0x00FF );	
	luma[1]   =  (yuv101010Pel[1]        & 0x00FF );	

	// Clamp the results to RGBA
	dstImage[y * dstImagePitch + x     ] = RGBAPACK_8bit(luma[0], luma[0], luma[0], constAlpha);
	dstImage[y * dstImagePitch + x + 1 ] = RGBAPACK_8bit(luma[1], luma[1], luma[1], constAlpha);
}


// CUDA kernel for outputing the final ARGB output from NV12;
extern "C"
__global__ void CudaNV12ToARGBKernel(	uint32 *srcImage,		uint32 nSourcePitch, 
										uint32 *dstImage,		uint32 nDestPitch,
										uint32 width,			uint32 height)
{
	int32 x, y;
	uint32 yuv101010Pel[2];
	uint32 processingPitch = ((width) + 63) & ~63;
	uint32 dstImagePitch   = nDestPitch >> 2;
	uint8 *srcImageU8     = (uint8 *)srcImage;

	processingPitch = nSourcePitch;

	// Pad borders with duplicate pixels, and we multiply by 2 because we process 2 pixels per thread
	x = blockIdx.x * (blockDim.x << 1) + (threadIdx.x << 1);
	y = blockIdx.y *  blockDim.y       +  threadIdx.y;

	if (x >= width)
		return; //x = width - 1;
	if (y >= height)
		return; // y = height - 1;

	// Read 2 Luma components at a time, so we don't waste processing since CbCr are decimated this way.
	// if we move to texture we could read 4 luminance values
	yuv101010Pel[0] = (srcImageU8[y * processingPitch + x    ]) << 2;
	yuv101010Pel[1] = (srcImageU8[y * processingPitch + x + 1]) << 2;

	uint32 chromaOffset    = processingPitch * height;
	int32 y_chroma = y >> 1;

	if (y & 1)  // odd scanline ?
	{
		uint32 chromaCb;
		uint32 chromaCr;

		chromaCb = srcImageU8[chromaOffset + y_chroma * processingPitch + x    ];
		chromaCr = srcImageU8[chromaOffset + y_chroma * processingPitch + x + 1];

		if (y_chroma < ((height >> 1) - 1)) // interpolate chroma vertically
		{
			chromaCb = (chromaCb + srcImageU8[chromaOffset + (y_chroma + 1) * processingPitch + x    ] + 1) >> 1;
			chromaCr = (chromaCr + srcImageU8[chromaOffset + (y_chroma + 1) * processingPitch + x + 1] + 1) >> 1;
		}

		yuv101010Pel[0] |= (chromaCb << ( COLOR_COMPONENT_BIT_SIZE       + 2));
		yuv101010Pel[0] |= (chromaCr << ((COLOR_COMPONENT_BIT_SIZE << 1) + 2));

		yuv101010Pel[1] |= (chromaCb << ( COLOR_COMPONENT_BIT_SIZE       + 2));
		yuv101010Pel[1] |= (chromaCr << ((COLOR_COMPONENT_BIT_SIZE << 1) + 2));
	}
	else
	{
		yuv101010Pel[0] |= ((uint32)srcImageU8[chromaOffset + y_chroma * processingPitch + x    ] << ( COLOR_COMPONENT_BIT_SIZE       + 2));
		yuv101010Pel[0] |= ((uint32)srcImageU8[chromaOffset + y_chroma * processingPitch + x + 1] << ((COLOR_COMPONENT_BIT_SIZE << 1) + 2));

		yuv101010Pel[1] |= ((uint32)srcImageU8[chromaOffset + y_chroma * processingPitch + x    ] << ( COLOR_COMPONENT_BIT_SIZE       + 2));
		yuv101010Pel[1] |= ((uint32)srcImageU8[chromaOffset + y_chroma * processingPitch + x + 1] << ((COLOR_COMPONENT_BIT_SIZE << 1) + 2));
	}

	// this steps performs the color conversion
	uint32 yuvi[6];
	float red[2], green[2], blue[2];

	yuvi[0] =  (yuv101010Pel[0] &   COLOR_COMPONENT_MASK    );	
	yuvi[1] = ((yuv101010Pel[0] >>  COLOR_COMPONENT_BIT_SIZE)       & COLOR_COMPONENT_MASK); 
	yuvi[2] = ((yuv101010Pel[0] >> (COLOR_COMPONENT_BIT_SIZE << 1)) & COLOR_COMPONENT_MASK);

	yuvi[3] =  (yuv101010Pel[1] &   COLOR_COMPONENT_MASK    );	
	yuvi[4] = ((yuv101010Pel[1] >>  COLOR_COMPONENT_BIT_SIZE)       & COLOR_COMPONENT_MASK); 
	yuvi[5] = ((yuv101010Pel[1] >> (COLOR_COMPONENT_BIT_SIZE << 1)) & COLOR_COMPONENT_MASK);

	// YUV to RGB Transformation conversion
	YUV2RGB(&yuvi[0], &red[0], &green[0], &blue[0]);
	YUV2RGB(&yuvi[3], &red[1], &green[1], &blue[1]);

	// Clamp the results to RGBA
	dstImage[y * dstImagePitch + x     ] = RGBAPACK_10bit(red[0], green[0], blue[0], constAlpha);
	dstImage[y * dstImagePitch + x + 1 ] = RGBAPACK_10bit(red[1], green[1], blue[1], constAlpha);
}

void		CudaNV12ToARGB(	uint32 *srcImage,		uint32 nSourcePitch, 
						   uint32 *dstImage,		uint32 nDestPitch,
						   uint32 width,			uint32 height)
{
	//
}