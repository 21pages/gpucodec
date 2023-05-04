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

#include "common_utils.h"
#include "common_directx11.h"

/* =======================================================
 * Windows implementation of OS-specific utility functions
 */

mfxStatus MFX_Initialize(mfxHDL deviceHandle, mfxIMPL impl, mfxVersion ver, MFXVideoSession* pSession, mfxFrameAllocator* pmfxAllocator, bool bCreateSharedHandles)
{
    mfxStatus sts = MFX_ERR_NONE;
    impl |= MFX_IMPL_VIA_D3D11;

    // Initialize Intel Media SDK Session
    sts = pSession->Init(impl, &ver);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    // If mfxFrameAllocator is provided it means we need to setup DirectX device and memory allocator
    if (pmfxAllocator) {
        // Provide device manager to Media SDK
        sts = pSession->SetHandle(DEVICE_MGR_TYPE, deviceHandle);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        pmfxAllocator->pthis  = *pSession; // We use Media SDK session ID as the allocation identifier
        pmfxAllocator->Alloc  = mfx_common_simple_alloc;
        pmfxAllocator->Free   = mfx_common_simple_free;
        pmfxAllocator->Lock   = mfx_common_simple_lock;
        pmfxAllocator->Unlock = mfx_common_simple_unlock;
        pmfxAllocator->GetHDL = mfx_common_simple_gethdl;

        // Since we are using video memory we must provide Media SDK with an external allocator
        sts = pSession->SetFrameAllocator(pmfxAllocator);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    return sts;
}

void MFX_Release()
{
    mfx_common_CleanupHWDevice();
}

void MFX_ClearYUVSurfaceVMem(mfxMemId memId)
{
    mfx_common_ClearYUVSurfaceD3D(memId);
}

void MFX_ClearRGBSurfaceVMem(mfxMemId memId)
{
    mfx_common_ClearRGBSurfaceD3D(memId);
}

