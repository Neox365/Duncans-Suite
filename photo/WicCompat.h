#pragma once
// MinGW-w64 liefert IWICBitmapSourceTransform (JPEG-DCT-Verkleinerung) nicht mit; das Windows-SDK von MSVC schon.
// Diese Deklaration entspricht der SDK-Schnittstelle und greift nur, wenn wincodec.h sie nicht selbst enthaelt.
#include <wincodec.h>

#ifndef __IWICBitmapSourceTransform_INTERFACE_DEFINED__
#define __IWICBitmapSourceTransform_INTERFACE_DEFINED__
#ifdef _MSC_VER
struct __declspec(uuid("3B16811B-6A43-4EC9-B713-3D5A0C13B940")) IWICBitmapSourceTransform : public IUnknown {
#else
struct IWICBitmapSourceTransform : public IUnknown {
#endif
    virtual HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT uiWidth, UINT uiHeight,
                                                 WICPixelFormatGUID* pguidDstFormat, WICBitmapTransformOptions dstTransform,
                                                 UINT nStride, UINT cbBufferSize, BYTE* pbBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClosestSize(UINT* puiWidth, UINT* puiHeight) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClosestPixelFormat(WICPixelFormatGUID* pguidDstFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE DoesSupportTransform(WICBitmapTransformOptions dstTransform, BOOL* pfIsSupported) = 0;
};
#ifdef __MINGW32__
__CRT_UUID_DECL(IWICBitmapSourceTransform, 0x3B16811B, 0x6A43, 0x4EC9, 0xB7, 0x13, 0x3D, 0x5A, 0x0C, 0x13, 0xB9, 0x40)
#endif
#endif
