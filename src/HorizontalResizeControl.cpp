#include "HorizontalResizeControl.h"
#include "ResourcesD2D1.h"

void mj::HorizontalResizeControl::Init(AllocatorBase* pAllocator)
{
  this->height = HEIGHT;
}

void mj::HorizontalResizeControl::Paint(ID2D1RenderTarget* pRenderTarget)
{
  auto pBrush = res::d2d1::ResizeControlBrush();
  if (pBrush)
  {
    pRenderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, this->width, this->height), pBrush);
  }
}

void mj::HorizontalResizeControl::Destroy()
{
}

bool mj::HorizontalResizeControl::OnLeftButtonDown(int16_t x, int16_t y)
{
  return true;
}