#ifndef CUDA_POSTPROCESSING_H_
#define CUDA_POSTPROCESSING_H_

#include <cuda.h>
#include <vector_types.h>
#include <cutil_inline.h>

typedef unsigned char   uint8;
typedef unsigned int    uint32;
typedef int             int32;

#define COLOR_COMPONENT_MASK            0x3FF
#define COLOR_COMPONENT_BIT_SIZE        10

#define FIXED_DECIMAL_POINT			    24
#define FIXED_POINT_MULTIPLIER		    1.0f
#define FIXED_COLOR_COMPONENT_MASK	    0xffffffff

typedef enum 
{
	ITU601 = 1,
	ITU709 = 2
} eColorSpace;

extern "C"
{
	CUresult	UpdateConstantMemory(float *hueCSC);
	
	void		SetColorSpaceMatrix(eColorSpace CSC, float *hueCSC);
	
	void		CudaNV12ToARGB(	uint32 *srcImage,		uint32 nSourcePitch, 
								uint32 *dstImage,		uint32 nDestPitch,
								uint32 width,			uint32 height);
};






#endif