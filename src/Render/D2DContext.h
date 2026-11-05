#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace Cutty
{
    // D3D11 디바이스 + DXGI 스왑체인 + Direct2D 디바이스 컨텍스트를 하나로 묶어
    // HWND 에 빠르게 렌더링할 수 있게 해주는 래퍼. (ID2D1HwndRenderTarget 대신
    // 최신 방식을 사용해 vsync 및 프레젠트 제어를 직접 담당한다.)
    class D2DContext
    {
    public:
        bool Initialize(HWND hwnd);
        void Resize(UINT width, UINT height);

        void BeginDraw();
        void EndDraw(bool vsync); // Present 까지 수행

        ID2D1DeviceContext* DeviceContext() const { return mD2DContext.Get(); }
        IDWriteFactory* DWriteFactory() const { return mDWriteFactory.Get(); }

    private:
        bool CreateTargetBitmap();

        Microsoft::WRL::ComPtr<ID3D11Device> mD3DDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> mD3DContext;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> mSwapChain;
        Microsoft::WRL::ComPtr<ID2D1Factory1> mD2DFactory;
        Microsoft::WRL::ComPtr<ID2D1Device> mD2DDevice;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> mD2DContext;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> mTargetBitmap;
        Microsoft::WRL::ComPtr<IDWriteFactory> mDWriteFactory;
        HWND mHwnd = nullptr;
    };
}
