﻿#include "FloatMagic.h"
#include "..\3rdparty\tracy\Tracy.hpp"
#include "vld.h"
#include "mj_win32.h"
#include "mj_common.h"
#include "ErrorExit.h"
#include "Direct2D.h"
#include "Threadpool.h"
#include "HexEdit.h"
#include "File.h"
#include <shellapi.h> // CommandLineToArgvW
#include <strsafe.h>
#include <ShlObj.h> // File dialog
#include "minicrt.h"

static constexpr WORD ID_FILE_OPEN = 40001;

static wchar_t s_pArg[MAX_PATH + 1];

static HWND s_MainWindowHandle;

HWND mj::GetMainWindowHandle()
{
  return s_MainWindowHandle;
}

wchar_t* mj::GetCommandLineArgument()
{
  return s_pArg;
}

namespace mj
{
  static void CreateMenu(HWND hWnd)
  {
    HMENU hMenu = ::CreateMenu();
    MJ_ERR_NULL(hMenu);

    MJ_UNINITIALIZED HMENU hSubMenu;
    MJ_ERR_NULL(hSubMenu = ::CreatePopupMenu());
    ::AppendMenuW(hSubMenu, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    MJ_ERR_ZERO(::AppendMenuW(hMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), L"&File"));

    MJ_ERR_NULL(hSubMenu = ::CreatePopupMenu());
    MJ_ERR_ZERO(::AppendMenuW(hMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), L"&Help"));

    // Loads DLLs: TextShaping
    MJ_ERR_ZERO(::SetMenu(hWnd, hMenu));
  }

  void OpenFileDialog(FloatMagic* pFloatMagic)
  {
    ZoneScoped;

    MJ_UNINITIALIZED ::IFileOpenDialog* pFileOpen;
    MJ_ERR_HRESULT(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog,
                                      reinterpret_cast<LPVOID*>(&pFileOpen)));

    if (SUCCEEDED(pFileOpen->Show(s_MainWindowHandle))) // Dialog result: User picked a file
    {
      MJ_UNINITIALIZED ::IShellItem* pItem;
      MJ_ERR_HRESULT(pFileOpen->GetResult(&pItem));

      MJ_UNINITIALIZED PWSTR pFileName;
      MJ_ERR_HRESULT(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pFileName));

      mj::LoadFileAsync(pFileName);

      ::CoTaskMemFree(pFileName);

      pItem->Release();
    }

    pFileOpen->Release();
  }

  /// <summary>
  /// Processes WM_COMMAND messages from the main WindowProc further.
  /// </summary>
  /// <param name="commandId"></param>
  void WindowProcCommand(FloatMagic* pFloatMagic, WORD commandId)
  {
    ZoneScoped;

    switch (commandId)
    {
    case ID_FILE_OPEN:
      mj::OpenFileDialog(pFloatMagic);
      break;
    }
  }

  LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    mj::FloatMagic* pMainWindow = reinterpret_cast<mj::FloatMagic*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
    {
      // Copy the lpParam from CreateWindowEx to this window's user data
      CREATESTRUCT* pcs = reinterpret_cast<CREATESTRUCT*>(lParam);
      pMainWindow       = reinterpret_cast<mj::FloatMagic*>(pcs->lpCreateParams);
      MJ_ERR_ZERO_VALID(::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pMainWindow)));

      // Make sure the main window handle is assigned before we start async initialization
      s_MainWindowHandle = hwnd;

      mj::CreateMenu(hwnd);
      mj::Direct2DInit(hwnd);
      mj::LoadFileAsync(mj::GetCommandLineArgument());
    }
      return ::DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_SIZE:
      mj::HexEditOnSize(HIWORD(lParam));
      mj::Direct2DOnSize(LOWORD(lParam), HIWORD(lParam));
      return 0;
    case WM_DESTROY:
      mj::Direct2DDestroy();
      ::PostQuitMessage(0);
      return 0;
    case WM_PAINT:
    {
      // Get vertical scroll bar position.
      MJ_UNINITIALIZED SCROLLINFO si;
      si.cbSize = sizeof(si);
      si.fMask  = SIF_POS;
      MJ_ERR_ZERO(::GetScrollInfo(hwnd, SB_VERT, &si));

      mj::Direct2DDraw(si.nPos);
      return 0;
    }
    case MJ_TASKEND:
    {
      mj::Task* pTask   = reinterpret_cast<mj::Task*>(wParam);
      mj::TaskEndFn pFn = reinterpret_cast<mj::TaskEndFn>(lParam);
      pFn(pTask->pContext);

      mj::ThreadpoolTaskFree(pTask);
    }
    break;
    case WM_COMMAND:
      mj::WindowProcCommand(pMainWindow, LOWORD(wParam));
      break;
    case WM_VSCROLL:
      mj::HexEditOnScroll(LOWORD(wParam));
      break;
    default:
      break;
    }

    return ::DefWindowProcW(hwnd, message, wParam, lParam);
  }
} // namespace mj

void mj::FloatMagicMain()
{
  // Parse command line
  LPWSTR pCommandLine = ::GetCommandLineW();
  MJ_UNINITIALIZED int numArgs;
  MJ_UNINITIALIZED LPWSTR* ppArgs;
  // Return value must be freed using LocalFree
  MJ_ERR_NULL(ppArgs = ::CommandLineToArgvW(pCommandLine, &numArgs));
  if (numArgs >= 2)
  {
    MJ_ERR_HRESULT(::StringCchCopyW(s_pArg, MJ_COUNTOF(s_pArg), ppArgs[1]));
  }
  MJ_ERR_NONNULL(::LocalFree(ppArgs));

  // Don't bloat the stack
  static mj::FloatMagic floatMagic;

  MJ_ERR_ZERO(::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0));
  mj::ThreadpoolInit();

  static constexpr const auto className = L"Class Name";

  WNDCLASS wc      = {};
  wc.lpfnWndProc   = mj::WindowProc;
  wc.hInstance     = HINST_THISCOMPONENT;
  wc.lpszClassName = className;
  MJ_ERR_ZERO(::RegisterClassW(&wc));

  // Loads DLLs: uxtheme, combase, msctf, oleaut32
  // We assign the returned HWND to static memory in the WM_CREATE message,
  // which is sooner, and also necessary if we want to start async
  // initialization on window creation.
  HWND hWnd = ::CreateWindowExW(0,                                                          // Optional window styles.
                                className,                                                  // Window class
                                L"Window Title",                                            // Window text
                                WS_OVERLAPPEDWINDOW | WS_VSCROLL,                           // Window style
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, // Size and position
                                nullptr,                                                    // Parent window
                                nullptr,                                                    // Menu
                                HINST_THISCOMPONENT,                                        // Instance handle
                                &floatMagic); // Additional application data
  MJ_ERR_NULL(hWnd);

  // If the window was previously visible, the return value is nonzero.
  // If the window was previously hidden, the return value is zero.
  static_cast<void>(::ShowWindow(hWnd, SW_SHOW));

  // Create accelerator table
  MJ_UNINITIALIZED HACCEL pAcceleratorTable;
  ACCEL table[] = { { FCONTROL | FVIRTKEY, 'O', ID_FILE_OPEN } };
  MJ_ERR_NULL(pAcceleratorTable = ::CreateAcceleratorTableW(table, 1));

  // Run the message loop.
  MSG msg = {};
  while (::GetMessageW(&msg, nullptr, 0, 0))
  {
    if (::TranslateAcceleratorW(hWnd, pAcceleratorTable, &msg))
    {
      // When TranslateAccelerator returns a nonzero value and the message is translated,
      // the application should not use the TranslateMessage function to process the message again.
      // Note MJ: Do not use DispatchMessage either.
      continue;
    }

    // If the message is translated, the return value is nonzero.
    // If the message is not translated, the return value is zero.
    static_cast<void>(::TranslateMessage(&msg));

    // The return value specifies the value returned by the window procedure.
    // Although its meaning depends on the message being dispatched,
    // the return value generally is ignored.
    static_cast<void>(::DispatchMessageW(&msg));
  }

  mj::ThreadpoolDestroy();
}
