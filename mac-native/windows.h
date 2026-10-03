#pragma once
// Подмножество WinAPI для учебных проектов с SetPixel/Ellipse.
// На macOS добавьте эту папку в include path и скомпилируйте gdi_compat.mm.
// На Windows используйте системный windows.h, без этого include path.

#include <stdint.h>
#include <stddef.h>

typedef void* HDC;
typedef void* HWND;
typedef void* HPEN;
typedef void* HBRUSH;
typedef void* HGDIOBJ;
typedef uint32_t COLORREF;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

#define RGB(r,g,b) \
    ((COLORREF)((BYTE)(r) | ((DWORD)(BYTE)(g) << 8) | ((DWORD)(BYTE)(b) << 16)))
#define CLR_INVALID ((COLORREF)0xFFFFFFFFu)
#define PS_SOLID 0
#define FALSE 0
#define TRUE 1

#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5

#ifdef __cplusplus
extern "C" {
#endif

HWND GetConsoleWindow(void);
HDC GetWindowDC(HWND hwnd);
int ReleaseDC(HWND hwnd, HDC hdc);
short GetAsyncKeyState(int vKey);
void Sleep(DWORD milliseconds);

COLORREF SetPixel(HDC hdc, int x, int y, COLORREF color);
HPEN CreatePen(int style, int width, COLORREF color);
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj);
int Ellipse(HDC hdc, int left, int top, int right, int bottom);
int DeleteObject(HGDIOBJ obj);

#ifdef __cplusplus
}
#endif
