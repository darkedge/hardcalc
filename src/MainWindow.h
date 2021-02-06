#pragma once
#include "HorizontalLayout.h"
#include "VerticalLayout.h"
#include "mj_win32.h"
#include "mj_allocator.h"
#include <d2d1_1.h>
#include <dxgi1_3.h>
#include <d3d11.h>
#include <dcomp.h>

namespace mj
{
  class MainWindow
  {
  private:
    static constexpr const size_t WIDTH  = 3;
    static constexpr const size_t HEIGHT = 2;
    Control* controls[WIDTH * HEIGHT];
    HorizontalLayout* pHorizontalLayouts[HEIGHT];
    VerticalLayout* pRootControl;
    ID2D1RenderTarget* pRenderTarget = nullptr;
    IDXGISwapChain1* pSwapChain      = nullptr;
    IDCompositionDevice* dcompDevice = nullptr;

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Resize();
    void OnPaint();
    void Destroy();

    void ReleaseUnusedObjects();

  public:
    void Run();
    void OnCreateID3D11Device(ID3D11Device* pD3d11Device, IDXGIDevice1* pDxgiDevice);
    void OnCreateID2D1RenderTarget(ID2D1RenderTarget* pRenderTarget, IDXGISwapChain1* pSwapChain,
                                   IDCompositionDevice* dcompDevice);
    void OnCreateIDXGISwapChain1(IDXGISwapChain1* pSwapChain);
  };

} // namespace mj
