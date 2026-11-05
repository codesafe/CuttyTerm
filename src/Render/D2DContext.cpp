#include "D2DContext.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace Cutty
{
    bool D2DContext::Initialize(HWND hwnd)
    {
        mHwnd = hwnd;

        UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
            nullptr, 0, D3D11_SDK_VERSION,
            mD3DDevice.GetAddressOf(), &featureLevel, mD3DContext.GetAddressOf());
        if (FAILED(hr)) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        mD3DDevice.As(&dxgiDevice);

        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, mD2DFactory.GetAddressOf());
        if (FAILED(hr)) return false;

        hr = mD2DFactory->CreateDevice(dxgiDevice.Get(), mD2DDevice.GetAddressOf());
        if (FAILED(hr)) return false;

        hr = mD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, mD2DContext.GetAddressOf());
        if (FAILED(hr)) return false;

        ComPtr<IDXGIAdapter> adapter;
        dxgiDevice->GetAdapter(adapter.GetAddressOf());
        ComPtr<IDXGIFactory2> dxgiFactory;
        adapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

        RECT rc;
        GetClientRect(hwnd, &rc);
        UINT width = static_cast<UINT>(rc.right - rc.left);
        UINT height = static_cast<UINT>(rc.bottom - rc.top);
        width = width == 0 ? 1 : width;
        height = height == 0 ? 1 : height;

        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = width;
        swapDesc.Height = height;
        swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = 2;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        hr = dxgiFactory->CreateSwapChainForHwnd(mD3DDevice.Get(), hwnd, &swapDesc, nullptr, nullptr, mSwapChain.GetAddressOf());
        if (FAILED(hr)) return false;

        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown**>(mDWriteFactory.GetAddressOf()));
        if (FAILED(hr)) return false;

        return CreateTargetBitmap();
    }

    bool D2DContext::CreateTargetBitmap()
    {
        ComPtr<IDXGISurface> surface;
        HRESULT hr = mSwapChain->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()));
        if (FAILED(hr)) return false;

        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

        hr = mD2DContext->CreateBitmapFromDxgiSurface(surface.Get(), &props, mTargetBitmap.GetAddressOf());
        if (FAILED(hr)) return false;

        mD2DContext->SetTarget(mTargetBitmap.Get());
        mD2DContext->SetDpi(96.0f, 96.0f); // 논리 픽셀 좌표계 사용 (WM_DPICHANGED 는 창 크기 조정으로 처리)
        return true;
    }

    void D2DContext::Resize(UINT width, UINT height)
    {
        if (!mSwapChain || width == 0 || height == 0) return;

        mD2DContext->SetTarget(nullptr);
        mTargetBitmap.Reset();

        HRESULT hr = mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) return;

        CreateTargetBitmap();
    }

    void D2DContext::BeginDraw()
    {
        mD2DContext->BeginDraw();
    }

    void D2DContext::EndDraw(bool vsync)
    {
        mD2DContext->EndDraw();
        mSwapChain->Present(vsync ? 1 : 0, 0);
    }
}
