// Copyright (c) 2019 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <stdio.h>
#include <memory>
#include <vector>

#include "mfxvideo++.h"
#include "mfxjpeg.h"
#include "mfxvp8.h"
#include "mfxplugin.h"

// =================================================================
// OS-specific definitions of types, macro, etc...
// The following should be defined:
//  - mfxTime
//  - MSDK_FOPEN
//  - MSDK_SLEEP
#if defined(_WIN32) || defined(_WIN64)
#include "bits/windows_defs.h"
#elif defined(__linux__)
#include "bits/linux_defs.h"
#endif

// =================================================================
// Helper macro definitions...
#define MSDK_PRINT_RET_MSG(ERR)         {PrintErrString(ERR, __FILE__, __LINE__);}
#define MSDK_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_CHECK_POINTER(P, ERR)      {if (!(P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_CHECK_ERROR(P, X, ERR)     {if ((X) == (P)) {MSDK_PRINT_RET_MSG(ERR); return ERR;}}
#define MSDK_IGNORE_MFX_STS(P, X)       {if ((X) == (P)) {P = MFX_ERR_NONE;}}
#define MSDK_BREAK_ON_ERROR(P)          {if (MFX_ERR_NONE != (P)) break;}
#define MSDK_SAFE_DELETE_ARRAY(P)       {if (P) {delete[] P; P = NULL;}}
#define MSDK_ALIGN32(X)                 (((mfxU32)((X)+31)) & (~ (mfxU32)31))
#define MSDK_ALIGN16(value)             (((value + 15) >> 4) << 4)
#define MSDK_SAFE_RELEASE(X)            {if (X) { X->Release(); X = NULL; }}
#define MSDK_MAX(A, B)                  (((A) > (B)) ? (A) : (B))

// Usage of the following two macros are only required for certain Windows DirectX11 use cases
#define WILL_READ  0x1000
#define WILL_WRITE 0x2000

// =================================================================
// Intel Media SDK memory allocator entrypoints....
// Implementation of this functions is OS/Memory type specific.
mfxStatus mfx_common_simple_alloc(mfxHDL pthis, mfxFrameAllocRequest* request, mfxFrameAllocResponse* response);
mfxStatus mfx_common_simple_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
mfxStatus mfx_common_simple_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
mfxStatus mfx_common_simple_gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL* handle);
mfxStatus mfx_common_simple_free(mfxHDL pthis, mfxFrameAllocResponse* response);

typedef mfxI32 msdkComponentType;
enum
{
    MSDK_VDECODE = 0x0001,
    MSDK_VENCODE = 0x0002,
    MSDK_VPP = 0x0004,
    MSDK_VENC = 0x0008,
};

static const mfxPluginUID MSDK_PLUGINGUID_NULL = { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };

// For use with asynchronous task management
typedef struct {
    mfxBitstream mfxBS;
    mfxSyncPoint syncp;
} Task;

void PrintErrString(int err,const char* filestr,int line);
void mfx_common_ClearYUVSurfaceVMem(mfxMemId memId);
void mfx_common_ClearRGBSurfaceVMem(mfxMemId memId);
mfxStatus mfx_common_Initialize(mfxHDL deviceHandle, mfxIMPL impl, mfxVersion ver, MFXVideoSession* pSession, mfxFrameAllocator* pmfxAllocator, bool bCreateSharedHandles = false);
void mfx_common_Release();

// Get free raw frame surface
int GetFreeSurfaceIndex(mfxFrameSurface1** pSurfacesPool, mfxU16 nPoolSize);
int GetFreeSurfaceIndex(const std::vector<mfxFrameSurface1>& pSurfacesPool);
// Get free task
int GetFreeTaskIndex(Task* pTaskPool, mfxU16 nPoolSize);