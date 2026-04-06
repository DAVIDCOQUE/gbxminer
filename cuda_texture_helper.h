// CUDA 12 Texture Object Helper Header
// Replaces deprecated cudaBindTexture with modern texture objects

#ifndef CUDA_TEXTURE_HELPER_H
#define CUDA_TEXTURE_HELPER_H

#include <cuda_runtime.h>

// Helper macro to create a 1D texture object from linear device memory
#define CREATE_TEXTURE_OBJECT_1D(texObj, p_devPtr, p_channelDesc, p_sizeInBytes) \
    do { \
        cudaResourceDesc _resDesc = {}; \
        _resDesc.resType = cudaResourceTypeLinear; \
        _resDesc.res.linear.desc = p_channelDesc; \
        _resDesc.res.linear.devPtr = p_devPtr; \
        _resDesc.res.linear.sizeInBytes = (size_t)(p_sizeInBytes); \
        cudaTextureDesc _texDesc = {}; \
        _texDesc.addressMode[0] = cudaAddressModeClamp; \
        _texDesc.filterMode = cudaFilterModePoint; \
        _texDesc.readMode = cudaReadModeElementType; \
        cudaCreateTextureObject(&(texObj), &_resDesc, &_texDesc, NULL); \
    } while(0)

// Helper macro to create a 2D texture object from pitch2D device memory
#define CREATE_TEXTURE_OBJECT_2D(texObj, p_devPtr, p_channelDesc, p_width, p_height, p_pitch) \
    do { \
        cudaResourceDesc _resDesc2D = {}; \
        _resDesc2D.resType = cudaResourceTypePitch2D; \
        _resDesc2D.res.pitch2D.desc = p_channelDesc; \
        _resDesc2D.res.pitch2D.devPtr = p_devPtr; \
        _resDesc2D.res.pitch2D.width = (size_t)(p_width); \
        _resDesc2D.res.pitch2D.height = (size_t)(p_height); \
        _resDesc2D.res.pitch2D.pitchInBytes = (size_t)(p_pitch); \
        cudaTextureDesc _texDesc2D = {}; \
        _texDesc2D.addressMode[0] = cudaAddressModeClamp; \
        _texDesc2D.addressMode[1] = cudaAddressModeClamp; \
        _texDesc2D.filterMode = cudaFilterModePoint; \
        _texDesc2D.readMode = cudaReadModeElementType; \
        cudaCreateTextureObject(&(texObj), &_resDesc2D, &_texDesc2D, NULL); \
    } while(0)

// Helper function to create a 1D texture object with standard settings
static inline cudaError_t createTextureObject1D(
    cudaTextureObject_t* texObject,
    void* devPtr,
    cudaChannelFormatDesc channelDesc,
    size_t sizeInBytes)
{
    cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypeLinear;
    resDesc.res.linear.desc = channelDesc;
    resDesc.res.linear.devPtr = devPtr;
    resDesc.res.linear.sizeInBytes = sizeInBytes;

    cudaTextureDesc texDesc = {};
    texDesc.addressMode[0] = cudaAddressModeClamp;
    texDesc.filterMode = cudaFilterModePoint;
    texDesc.readMode = cudaReadModeElementType;

    return cudaCreateTextureObject(texObject, &resDesc, &texDesc, NULL);
}

// Helper function to create a 2D texture object with standard settings
static inline cudaError_t createTextureObject2D(
    cudaTextureObject_t* texObject,
    void* devPtr,
    cudaChannelFormatDesc channelDesc,
    size_t width,
    size_t height,
    size_t pitchInBytes)
{
    cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypePitch2D;
    resDesc.res.pitch2D.desc = channelDesc;
    resDesc.res.pitch2D.devPtr = devPtr;
    resDesc.res.pitch2D.width = width;
    resDesc.res.pitch2D.height = height;
    resDesc.res.pitch2D.pitchInBytes = pitchInBytes;

    cudaTextureDesc texDesc = {};
    texDesc.addressMode[0] = cudaAddressModeClamp;
    texDesc.addressMode[1] = cudaAddressModeClamp;
    texDesc.filterMode = cudaFilterModePoint;
    texDesc.readMode = cudaReadModeElementType;

    return cudaCreateTextureObject(texObject, &resDesc, &texDesc, NULL);
}

#endif // CUDA_TEXTURE_HELPER_H
