#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <dwrite.h>
#include <d2d1.h>
#include <shellscalingapi.h> // MDT_EFFECTIVE_DPI

#include "mj_common.h"
#include "loremipsum.h"
#include "mj_gapbuffer.h"
#include "mj_win32.h"
#include <string>
#include <vector>

static constexpr size_t BUFFER_SIZE = 2 * 1024 * 1024; // 2 MiB
static constexpr UINT WINDOW_WIDTH  = 640;
static constexpr UINT WINDOW_HEIGHT = 480;
static constexpr DWORD dwStyle      = WS_OVERLAPPEDWINDOW;

// One logical inch equals 96 pixels. // TODO: This can change!
static constexpr float MJ_96_DPI = 96.0f;
// In typography, the size of type is measured in units called points. One point equals 1/72 of an inch.
static constexpr float MJ_POINT = (1.0f / 72.0f);
static float s_BaseDpiScaleInv;

// __ImageBase is better than GetCurrentModule()
// Can be cast to a HINSTANCE
#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

static HWND s_Hwnd;
static std::wstring s_Line;

static HCURSOR s_cursorType;

// how much to scale a design that assumes 96-DPI pixels
static float s_DpiScale;

// Direct2D
static Microsoft::WRL::ComPtr<ID2D1Factory> s_pD2DFactory;
static Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> s_pRenderTarget;
static Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> s_pBrush;

// DirectWrite
static Microsoft::WRL::ComPtr<IDWriteFactory> s_pDWriteFactory;
static Microsoft::WRL::ComPtr<IDWriteTextFormat> s_pTextFormat;
static std::vector<Microsoft::WRL::ComPtr<IDWriteTextLayout>> s_Lines;

static mj::GapBuffer s_GapBuffer;

static void OnResize(UINT width, UINT height)
{
  if (s_pRenderTarget)
  {
    D2D1_SIZE_U size;
    size.width  = width;
    size.height = height;

    s_pRenderTarget->Resize(size);
  }
}

static HRESULT CreateDeviceResources()
{
  HRESULT hr = S_OK;

  RECT rect;
  GetClientRect(s_Hwnd, &rect);

  D2D1_SIZE_U size = D2D1::SizeU(rect.right - rect.left, rect.bottom - rect.top);

  if (!s_pRenderTarget)
  {
    // Create a Direct2D render target.
    hr = s_pD2DFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                               D2D1::HwndRenderTargetProperties(s_Hwnd, size),
                                               s_pRenderTarget.ReleaseAndGetAddressOf());

    // Create a black brush.
    if (SUCCEEDED(hr))
    {
      hr = s_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), s_pBrush.ReleaseAndGetAddressOf());
    }
  }

  // Create a text layout using the text format.
  if (SUCCEEDED(hr))
  {
    float width  = rect.right * s_BaseDpiScaleInv;
    float height = rect.bottom * s_BaseDpiScaleInv;

    wchar_t buf[1024]; // TODO: Small buffer!
    int numBytes =
        mj::win32::Widen(buf, s_GapBuffer.pBufBegin, (int)(s_GapBuffer.pGapBegin - s_GapBuffer.pBufBegin), sizeof(buf));
    numBytes += mj::win32::Widen(&buf[numBytes], s_GapBuffer.pGapEnd, (int)(s_GapBuffer.pBufEnd - s_GapBuffer.pGapEnd),
                                 sizeof(buf) - numBytes);
    s_Line = std::wstring(buf);

    s_Lines.resize(1);
    hr = s_pDWriteFactory->CreateTextLayout(
        s_Line.c_str(),               // The string to be laid out and formatted.
        (UINT32)(s_Line.length()),    // The length of the string.
        s_pTextFormat.Get(), // The text format to apply to the string (contains font information, etc).
        width,                        // The width of the layout box.
        height,                       // The height of the layout box.
        s_Lines[0].ReleaseAndGetAddressOf() // The IDWriteTextLayout interface pointer.
    );

    size_t position             = mj::GapBufferGetVirtualCursorPosition(&s_GapBuffer);
    DWRITE_TEXT_RANGE textRange = { (UINT32)position, 1 };
    s_Lines[0]->SetUnderline(true, textRange);
  }

  return hr;
}

static HRESULT TextDraw()
{
  MJ_UNINITIALIZED RECT rect;

  GetClientRect(s_Hwnd, &rect);

  // Create a D2D rect that is the same size as the window.
  D2D1_RECT_F layoutRect =
      D2D1::RectF(static_cast<FLOAT>(rect.top) * s_BaseDpiScaleInv, static_cast<FLOAT>(rect.left) / s_BaseDpiScaleInv,
                  static_cast<FLOAT>(rect.right - rect.left) / s_BaseDpiScaleInv,
                  static_cast<FLOAT>(rect.bottom - rect.top) / s_BaseDpiScaleInv);

  // Use the DrawTextLayout method of the D2D render target interface to draw.
  for(auto pTextLayout : s_Lines)
  {
  s_pRenderTarget->DrawTextLayout(D2D1_POINT_2F{ 0.0f, 0.0f }, pTextLayout.Get(), s_pBrush.Get(),
                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
  return S_OK;
}

static HRESULT DrawD2DContent()
{
  HRESULT hr = CreateDeviceResources();

  if (!(s_pRenderTarget->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED))
  {
    s_pRenderTarget->BeginDraw();
    s_pRenderTarget->SetTransform(D2D1::IdentityMatrix());
    s_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::WhiteSmoke));

    if (SUCCEEDED(hr))
    {
      TextDraw();
    }

    if (SUCCEEDED(hr))
    {
      hr = s_pRenderTarget->EndDraw();
    }
  }

  if (FAILED(hr))
  {
    s_pRenderTarget.Reset();
    s_pBrush.Reset();
  }

  return hr;
}

static void CalculateDpiScale()
{
  UINT dpi   = GetDpiForWindow(s_Hwnd);
  s_DpiScale = (float)dpi / MJ_96_DPI;
}

static FLOAT ConvertPointSizeToDIP(FLOAT points)
{
  return (points * MJ_POINT) * MJ_96_DPI * s_DpiScale * s_BaseDpiScaleInv;
}

static HRESULT CreateDeviceIndependentResources()
{
  HRESULT hr;

  // Create Direct2D factory.
#ifdef _DEBUG
  D2D1_FACTORY_OPTIONS options;
  options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, s_pD2DFactory.ReleaseAndGetAddressOf());
#else
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, s_pD2DFactory.ReleaseAndGetAddressOf());
#endif // _DEBUG

  // Create a shared DirectWrite factory.
  if (SUCCEEDED(hr))
  {
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(s_pDWriteFactory.ReleaseAndGetAddressOf()));
  }

  // This sets the default font, weight, stretch, style, and locale.
  if (SUCCEEDED(hr))
  {
    hr = s_pDWriteFactory->CreateTextFormat(
        L"Consolas", // Font family name.
        nullptr,     // Font collection (nullptr sets it to use the system font collection).
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, ConvertPointSizeToDIP(11.0f),
        L"en-us", s_pTextFormat.ReleaseAndGetAddressOf());
  }

  if (SUCCEEDED(hr))
  {
    s_pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  }

  return hr;
}

static void OnClick(UINT x, UINT y)
{
  DWRITE_HIT_TEST_METRICS hitTestMetrics;
  BOOL isTrailingHit;
  BOOL isInside;

  s_Lines[0]->HitTestPoint(((FLOAT)x) * s_BaseDpiScaleInv, ((FLOAT)y) / s_BaseDpiScaleInv, &isTrailingHit,
                              &isInside, &hitTestMetrics);

  if (isInside == TRUE)
  {
    BOOL underline;
    s_Lines[0]->GetUnderline(hitTestMetrics.textPosition, &underline);
    DWRITE_TEXT_RANGE textRange = { hitTestMetrics.textPosition, 1 };
    s_Lines[0]->SetUnderline(!underline, textRange);
  }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {
  case WM_LBUTTONDOWN:
  {
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    OnClick(pt.x, pt.y);
    if (DragDetect(hwnd, pt))
    {
      // Start dragging.
    }
    DrawD2DContent();
  }
  break;
  case WM_CHAR:
    if (iswprint((wint_t)wParam))
    {
      mj::GapBufferInsertCharacterAtCursor(&s_GapBuffer, (wchar_t)wParam);
      DrawD2DContent();
    }
    break;
  case WM_KEYDOWN:
    switch (wParam)
    {
    case VK_HOME:
      mj::GapBufferJumpStartOfLine(&s_GapBuffer);
      DrawD2DContent();
      break;
    case VK_END:
      mj::GapBufferJumpEndOfLine(&s_GapBuffer);
      DrawD2DContent();
      break;
    case VK_LEFT:
      mj::GapBufferDecrementCursor(&s_GapBuffer);
      DrawD2DContent();
      break;
    case VK_RIGHT:
      mj::GapBufferIncrementCursor(&s_GapBuffer);
      DrawD2DContent();
      break;
    case VK_DELETE:
      mj::GapBufferDeleteAtCursor(&s_GapBuffer);
      DrawD2DContent();
      break;
    case VK_BACK:
      mj::GapBufferBackspaceAtCursor(&s_GapBuffer);
      DrawD2DContent();
      break;
    }
    break;
  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT)
    {
      SetCursor(s_cursorType);
      return TRUE;
    }
    break;
  case WM_SIZE:
  {
    UINT width  = LOWORD(lParam);
    UINT height = HIWORD(lParam);
    OnResize(width, height);
  }
  break;
  case WM_DPICHANGED:
  {
    CalculateDpiScale();
    CreateDeviceIndependentResources();

    // Ensure the client area is scaled properly
    RECT* pRect           = (RECT*)lParam;
    RECT windowRect       = { 0, 0, (LONG)(WINDOW_WIDTH * s_DpiScale), (LONG)(WINDOW_HEIGHT * s_DpiScale) };
    const bool hasMenu    = false;
    const DWORD dwExStyle = 0;
    UINT dpi              = GetDpiForWindow(s_Hwnd);
    AdjustWindowRectExForDpi(&windowRect, dwStyle, hasMenu, dwExStyle, dpi);
    windowRect.right  = pRect->left + (windowRect.right - windowRect.left);
    windowRect.bottom = pRect->top + (windowRect.bottom - windowRect.top);
    windowRect.left   = pRect->left;
    windowRect.top    = pRect->top;

    SetWindowPos(hwnd, NULL, windowRect.left, windowRect.top, windowRect.right - windowRect.left,
                 windowRect.bottom - windowRect.top, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  break;
  case WM_PAINT:
  case WM_DISPLAYCHANGE:
    ValidateRect(hwnd, nullptr);
    DrawD2DContent();
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  }

  return DefWindowProcW(hwnd, message, wParam, lParam);
}

HMONITOR GetPrimaryMonitor()
{
  return MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR pCmdLine, _In_ int nCmdShow)
{
  (void)hInstance;
  (void)hPrevInstance;
  (void)pCmdLine;
  (void)nCmdShow;

  HRESULT hr = S_OK;

  // Init memory for buffer
  void* pMemory = VirtualAlloc(0, BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!pMemory)
  {
    hr = E_FAIL;
  }

  if (SUCCEEDED(hr))
  {
    mj::GapBufferInit(&s_GapBuffer, pMemory, ((char*)pMemory) + BUFFER_SIZE);
    mj::GapBufferSetText(&s_GapBuffer, pLoremIpsum);

    // Register window class.
    {
      MJ_UNINITIALIZED WNDCLASSEX wcex;
      wcex.cbSize        = sizeof(WNDCLASSEX);
      wcex.style         = CS_HREDRAW | CS_VREDRAW;
      wcex.lpfnWndProc   = WndProc;
      wcex.cbClsExtra    = 0;
      wcex.cbWndExtra    = sizeof(LONG_PTR);
      wcex.hInstance     = HINST_THISCOMPONENT;
      wcex.hbrBackground = nullptr;
      wcex.lpszMenuName  = nullptr;
      wcex.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
      wcex.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
      wcex.lpszClassName = TEXT("DemoApp");
      wcex.hIconSm       = LoadIconW(nullptr, IDI_APPLICATION);

      hr = RegisterClassExW(&wcex) ? S_OK : E_FAIL;
    }

    s_cursorType = LoadCursorW(nullptr, IDC_IBEAM);
  }

  // We currently assume that the application will always be created on the primary monitor.
  MJ_UNINITIALIZED UINT dpiX;
  MJ_UNINITIALIZED UINT dpiY;
  hr = GetDpiForMonitor(GetPrimaryMonitor(), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  if (SUCCEEDED(hr))
  {
    s_DpiScale        = (float)dpiX / MJ_96_DPI;
    s_BaseDpiScaleInv = s_DpiScale;
  }

  if (SUCCEEDED(hr))
  {
    hr = CreateDeviceIndependentResources();
  }

  if (SUCCEEDED(hr))
  {
    // Get window rectangle
    RECT windowRect       = { 0, 0, (LONG)(WINDOW_WIDTH * s_DpiScale), (LONG)(WINDOW_HEIGHT * s_DpiScale) };
    const bool hasMenu    = false;
    const DWORD dwExStyle = 0;
    AdjustWindowRectExForDpi(&windowRect, dwStyle, hasMenu, dwExStyle, dpiX);
    const LONG windowWidth  = windowRect.right - windowRect.left;
    const LONG windowHeight = windowRect.bottom - windowRect.top;

    // Create window.
    s_Hwnd = CreateWindowExW(0, TEXT("DemoApp"), TEXT("hardcalc"), dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                             static_cast<int>(windowWidth), static_cast<int>(windowHeight), nullptr, nullptr,
                             HINST_THISCOMPONENT, nullptr);
  }

  if (SUCCEEDED(hr))
  {
    hr = s_Hwnd ? S_OK : E_FAIL;
  }

  // Draw initial contents to prevent a blank screen flash
  if (SUCCEEDED(hr))
  {
    hr = DrawD2DContent();
  }

  if (SUCCEEDED(hr))
  {
    ShowWindow(s_Hwnd, SW_SHOWNORMAL);
    UpdateWindow(s_Hwnd);
  }

  if (SUCCEEDED(hr))
  {
    MSG msg;

    while (GetMessageW(&msg, nullptr, 0, 0))
    {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  MJ_DISCARD(VirtualFree(pMemory, 0, MEM_RELEASE));

  return 0;
}
