#pragma once
#include <dwrite.h>
#include "mj_gapbuffer.h"
#include "mj_common.h"

struct ID2D1HwndRenderTarget;
struct RenderTargetResources;

namespace mj
{
  struct ECursor
  {
    enum Enum
    {
      ARROW,
      IBEAM,
      WAIT,
      CROSS,
      UPARROW,
      SIZENWSE,
      SIZENESW,
      SIZEWE,
      SIZENS,
      SIZEALL,
      NO,
      HAND,
      APPSTARTING,
      HELP,
      PIN,
      PERSON,
    };
  };

  struct RenderedLine
  {
    wchar_t* pText                 = nullptr;
    size_t textLength              = 0;
    IDWriteTextLayout* pTextLayout = nullptr;
  };

  enum class EDraggable
  {
    NONE,
    TEXT_SELECT,
    TEXT_MOVE,
    HOR_SCROLLBAR,
    VER_SCROLLBAR
  };

  struct DragAction
  {
    EDraggable draggable;
    FLOAT start;
    SHORT mouseStartX;
    SHORT mouseStartY;
  };

  class TextEdit
  {
  private:
    struct Reverse
    {
      RECT horScrollbarRect;
    };

    void DrawHorizontalScrollBar(ID2D1HwndRenderTarget* pRenderTarget, RenderTargetResources* pResources);

    void* pMemory;
    RenderedLine* pLines;
    mj::GapBuffer buf;
    D2D1_RECT_F widgetRect;  // Rect of widget inside rendertarget
    D2D1_POINT_2F scrollPos; // Position of scroll area
    FLOAT width;             // Equal to width of the longest rendered line
    DragAction drag;
    Reverse reverse;

  public:
    HRESULT CreateDeviceResources(IDWriteFactory* pFactory, IDWriteTextFormat* pTextFormat, FLOAT width, FLOAT height);
    void MouseDown(SHORT x, SHORT y);
    void MouseUp();
    ECursor::Enum MouseMove(SHORT x, SHORT y);
    HRESULT Init(FLOAT left, FLOAT top, FLOAT right, FLOAT bottom);
    void WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Draw(ID2D1HwndRenderTarget* pRenderTarget, RenderTargetResources* pResources);
    void Destroy();

    MJ_CRGETTER(GetDragAction, drag);
  };
} // namespace mj
