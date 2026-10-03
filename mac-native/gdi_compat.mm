// Учебное подмножество WinAPI/GDI поверх Cocoa, без изменения main().
// Рисование хранится в bitmap: окно можно перекрывать, а длительное
// движение фигур не накапливает историю команд и не увеличивает память.

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include "windows.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_set>

static const int kW = 700;
static const int kH = 700;
static std::array<BYTE, kW * kH * 4> gPixels;
static CGContextRef gCanvas = nullptr;
static NSWindow *gWindow = nil;
static NSView *gView = nil;
static bool gClosed = false;
static bool gKeyDown[256] = {};
static bool gKeyPressed[256] = {};
static int gDeviceContext;
static bool gDebug = false;
static bool gAutoplay = false;
static double gLastAutoplay = 0;

struct PenData { COLORREF color; int width; };
static PenData gDefaultPen = {RGB(0, 0, 0), 1};
static PenData *gSelectedPen = &gDefaultPen;
static std::unordered_set<PenData *> gPens;

static bool EnvEnabled(const char *name)
{
    const char *value = std::getenv(name);
    return value && std::strcmp(value, "1") == 0;
}

static void SetKeyState(int key, bool down)
{
    if (key < 0 || key >= 256) return;
    if (down && !gKeyDown[key]) gKeyPressed[key] = true;
    gKeyDown[key] = down;
}

static void ClearKeyStates()
{
    std::fill(std::begin(gKeyDown), std::end(gKeyDown), false);
    std::fill(std::begin(gKeyPressed), std::end(gKeyPressed), false);
}

// Физические клавиши: VK_A..VK_Z работают также при русской раскладке.
static int VirtualKeyForEvent(NSEvent *event)
{
    switch (event.keyCode) {
        case kVK_ANSI_A: return 'A'; case kVK_ANSI_B: return 'B';
        case kVK_ANSI_C: return 'C'; case kVK_ANSI_D: return 'D';
        case kVK_ANSI_E: return 'E'; case kVK_ANSI_F: return 'F';
        case kVK_ANSI_G: return 'G'; case kVK_ANSI_H: return 'H';
        case kVK_ANSI_I: return 'I'; case kVK_ANSI_J: return 'J';
        case kVK_ANSI_K: return 'K'; case kVK_ANSI_L: return 'L';
        case kVK_ANSI_M: return 'M'; case kVK_ANSI_N: return 'N';
        case kVK_ANSI_O: return 'O'; case kVK_ANSI_P: return 'P';
        case kVK_ANSI_Q: return 'Q'; case kVK_ANSI_R: return 'R';
        case kVK_ANSI_S: return 'S'; case kVK_ANSI_T: return 'T';
        case kVK_ANSI_U: return 'U'; case kVK_ANSI_V: return 'V';
        case kVK_ANSI_W: return 'W'; case kVK_ANSI_X: return 'X';
        case kVK_ANSI_Y: return 'Y'; case kVK_ANSI_Z: return 'Z';
        case kVK_ANSI_0: return '0'; case kVK_ANSI_1: return '1';
        case kVK_ANSI_2: return '2'; case kVK_ANSI_3: return '3';
        case kVK_ANSI_4: return '4'; case kVK_ANSI_5: return '5';
        case kVK_ANSI_6: return '6'; case kVK_ANSI_7: return '7';
        case kVK_ANSI_8: return '8'; case kVK_ANSI_9: return '9';
        case kVK_ANSI_Keypad0: return VK_NUMPAD0;
        case kVK_ANSI_Keypad1: return VK_NUMPAD1;
        case kVK_ANSI_Keypad2: return VK_NUMPAD2;
        case kVK_ANSI_Keypad3: return VK_NUMPAD3;
        case kVK_ANSI_Keypad4: return VK_NUMPAD4;
        case kVK_ANSI_Keypad5: return VK_NUMPAD5;
        case kVK_ANSI_Keypad6: return VK_NUMPAD6;
        case kVK_ANSI_Keypad7: return VK_NUMPAD7;
        case kVK_ANSI_Keypad8: return VK_NUMPAD8;
        case kVK_ANSI_Keypad9: return VK_NUMPAD9;
        case kVK_ANSI_KeypadMultiply: return VK_MULTIPLY;
        case kVK_ANSI_KeypadPlus: return VK_ADD;
        case kVK_ANSI_KeypadMinus: return VK_SUBTRACT;
        case kVK_ANSI_KeypadDecimal: return VK_DECIMAL;
        case kVK_ANSI_KeypadDivide: return VK_DIVIDE;
        case kVK_Return: case kVK_ANSI_KeypadEnter: return VK_RETURN;
        case kVK_Delete: return VK_BACK;
        case kVK_ForwardDelete: return VK_DELETE;
        case kVK_Tab: return VK_TAB; case kVK_Space: return VK_SPACE;
        case kVK_Escape: return VK_ESCAPE;
        case kVK_LeftArrow: return VK_LEFT; case kVK_RightArrow: return VK_RIGHT;
        case kVK_UpArrow: return VK_UP; case kVK_DownArrow: return VK_DOWN;
        case kVK_Home: return VK_HOME; case kVK_End: return VK_END;
        case kVK_PageUp: return VK_PRIOR; case kVK_PageDown: return VK_NEXT;
        case kVK_F1: return VK_F1; case kVK_F2: return VK_F2;
        case kVK_F3: return VK_F3; case kVK_F4: return VK_F4;
        case kVK_F5: return VK_F5; case kVK_F6: return VK_F6;
        case kVK_F7: return VK_F7; case kVK_F8: return VK_F8;
        case kVK_F9: return VK_F9; case kVK_F10: return VK_F10;
        case kVK_F11: return VK_F11; case kVK_F12: return VK_F12;
        default: return -1;
    }
}

@interface GDICompatView : NSView
@end

@implementation GDICompatView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    CGImageRef bitmap = CGBitmapContextCreateImage(gCanvas);
    if (!bitmap) return;
    NSImage *image = [[NSImage alloc] initWithCGImage:bitmap size:NSMakeSize(kW, kH)];
    [image drawInRect:self.bounds fromRect:NSZeroRect
           operation:NSCompositingOperationCopy fraction:1.0 respectFlipped:YES hints:nil];
    [image release];
    CGImageRelease(bitmap);
}

- (void)keyDown:(NSEvent *)event { SetKeyState(VirtualKeyForEvent(event), true); }
- (void)keyUp:(NSEvent *)event { SetKeyState(VirtualKeyForEvent(event), false); }

- (void)flagsChanged:(NSEvent *)event
{
    const NSEventModifierFlags flags = event.modifierFlags;
    SetKeyState(VK_SHIFT, (flags & NSEventModifierFlagShift) != 0);
    SetKeyState(VK_CONTROL, (flags & NSEventModifierFlagControl) != 0);
    SetKeyState(VK_MENU, (flags & NSEventModifierFlagOption) != 0);
    // Device bits различают одновременно удерживаемые левую/правую клавиши.
    SetKeyState(VK_LSHIFT, (flags & NX_DEVICELSHIFTKEYMASK) != 0);
    SetKeyState(VK_RSHIFT, (flags & NX_DEVICERSHIFTKEYMASK) != 0);
    SetKeyState(VK_LCONTROL, (flags & NX_DEVICELCTLKEYMASK) != 0);
    SetKeyState(VK_RCONTROL, (flags & NX_DEVICERCTLKEYMASK) != 0);
    SetKeyState(VK_LMENU, (flags & NX_DEVICELALTKEYMASK) != 0);
    SetKeyState(VK_RMENU, (flags & NX_DEVICERALTKEYMASK) != 0);
    SetKeyState(VK_LWIN, (flags & NX_DEVICELCMDKEYMASK) != 0);
    SetKeyState(VK_RWIN, (flags & NX_DEVICERCMDKEYMASK) != 0);
}
@end

@interface GDICompatWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation GDICompatWindowDelegate
- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    ClearKeyStates();
}
- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    gClosed = true;
}
@end
static GDICompatWindowDelegate *gWindowDelegate = nil;

static void SetColor(COLORREF color)
{
    CGContextSetRGBFillColor(gCanvas, (color & 255) / 255.0,
                            ((color >> 8) & 255) / 255.0,
                            ((color >> 16) & 255) / 255.0, 1);
    CGContextSetRGBStrokeColor(gCanvas, (color & 255) / 255.0,
                              ((color >> 8) & 255) / 255.0,
                              ((color >> 16) & 255) / 255.0, 1);
}

static void EnsureAppAndWindow()
{
    if (gWindow) return;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        gCanvas = CGBitmapContextCreate(gPixels.data(), kW, kH, 8, kW * 4, space,
                                       kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if (!gCanvas) {
            std::fprintf(stderr, "Не удалось создать поверхность рисования macOS.\n");
            std::exit(1);
        }
        CGContextTranslateCTM(gCanvas, 0, kH);
        CGContextScaleCTM(gCanvas, 1, -1); // GDI: начало сверху слева, Y вниз.
        CGContextSetShouldAntialias(gCanvas, false); // Hide стирает контур полностью.

        // В исходных Point/Circle/Face Hide() рисует синим цветом.
        COLORREF background = RGB(0, 0, 255);
        if (const char *value = std::getenv("GDI_BACKGROUND")) {
            int r, g, b; char extra;
            if (std::sscanf(value, "%d,%d,%d%c", &r, &g, &b, &extra) == 3 &&
                r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255)
                background = RGB(r, g, b);
        }
        SetColor(background);
        CGContextFillRect(gCanvas, CGRectMake(0, 0, kW, kH));
        gDebug = EnvEnabled("GDI_DEBUG");
        gAutoplay = EnvEnabled("GDI_AUTOPLAY");
        gLastAutoplay = CFAbsoluteTimeGetCurrent();

        gWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, kW, kH)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable
            backing:NSBackingStoreBuffered defer:NO];
        NSString *title = [[NSProcessInfo processInfo] processName];
        if (const char *value = std::getenv("GDI_TITLE")) {
            NSString *customTitle = [NSString stringWithUTF8String:value];
            if (customTitle.length > 0) title = customTitle;
        }
        [gWindow setTitle:title];
        [gWindow setReleasedWhenClosed:NO];
        gWindowDelegate = [[GDICompatWindowDelegate alloc] init];
        [gWindow setDelegate:gWindowDelegate];
        gView = [[GDICompatView alloc] initWithFrame:NSMakeRect(0, 0, kW, kH)];
        [gWindow setContentView:gView];
        [NSApp finishLaunching];
        [gWindow makeKeyAndOrderFront:nil];
        [gWindow makeFirstResponder:gView];
        [NSApp activateIgnoringOtherApps:YES];
        [gWindow displayIfNeeded];
    }
}

static void PumpEvents(double waitSeconds = 0.008)
{
    EnsureAppAndWindow();
    @autoreleasepool {
        // Ожидание в Cocoa вместо холостого while(1), сохраняя отзывчивость.
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
            untilDate:[NSDate dateWithTimeIntervalSinceNow:waitSeconds]
            inMode:NSDefaultRunLoopMode dequeue:YES];
        while (event) {
            [NSApp sendEvent:event];
            event = [NSApp nextEventMatchingMask:NSEventMaskAny
                untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
        }
        [NSApp updateWindows];
    }
    // main() учебных программ не имеет обработчика закрытия окна.
    if (gClosed) std::exit(0);
}

static bool ValidDC(HDC hdc) { return gCanvas && hdc == &gDeviceContext; }
static bool ValidPen(PenData *pen) { return pen == &gDefaultPen || gPens.count(pen) != 0; }

extern "C" {

HWND GetConsoleWindow(void)
{
    EnsureAppAndWindow();
    return (HWND)gWindow;
}

HDC GetWindowDC(HWND hwnd)
{
    if (!gWindow || hwnd != (HWND)gWindow || gClosed) return nullptr;
    return &gDeviceContext;
}

int ReleaseDC(HWND hwnd, HDC hdc)
{
    // Единственная поверхность живёт до завершения процесса.
    return hwnd == (HWND)gWindow && ValidDC(hdc);
}

short GetAsyncKeyState(int vKey)
{
    PumpEvents();
    if (vKey < 0 || vKey >= 256) return 0;
    if (gAutoplay) {
        const double now = CFAbsoluteTimeGetCurrent();
        if (now - gLastAutoplay < 0.4) return 0;
        gLastAutoplay = now;
        return (short)0x8001;
    }
    const unsigned state = (gKeyDown[vKey] ? 0x8000u : 0u) |
                           (gKeyPressed[vKey] ? 1u : 0u);
    gKeyPressed[vKey] = false;
    return (short)state;
}

void Sleep(DWORD milliseconds)
{
    const double deadline = CFAbsoluteTimeGetCurrent() + milliseconds / 1000.0;
    do {
        PumpEvents(std::min(0.008, std::max(0.0, deadline - CFAbsoluteTimeGetCurrent())));
    } while (CFAbsoluteTimeGetCurrent() < deadline);
}

COLORREF SetPixel(HDC hdc, int x, int y, COLORREF color)
{
    if (!ValidDC(hdc) || x < 0 || x >= kW || y < 0 || y >= kH) return CLR_INVALID;
    SetColor(color);
    CGContextFillRect(gCanvas, CGRectMake(x, y, 1, 1));
    [gView setNeedsDisplay:YES];
    if (gDebug)
        std::fprintf(stderr, "[gdi] SetPixel x=%d y=%d color=%06x\n", x, y, color);
    return color & 0x00FFFFFFu;
}

HPEN CreatePen(int style, int width, COLORREF color)
{
    if (style != PS_SOLID || width < 0) return nullptr;
    PenData *pen = new (std::nothrow) PenData{color, std::max(1, width)};
    if (pen) gPens.insert(pen);
    return pen;
}

HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj)
{
    PenData *pen = static_cast<PenData *>(obj);
    if (!ValidDC(hdc) || !ValidPen(pen)) return nullptr;
    PenData *previous = gSelectedPen;
    gSelectedPen = pen;
    return previous;
}

int Ellipse(HDC hdc, int left, int top, int right, int bottom)
{
    if (!ValidDC(hdc) || left == right || top == bottom) return FALSE;
    const double x = std::min(left, right), y = std::min(top, bottom);
    const double width = std::max(left, right) - x;
    const double height = std::max(top, bottom) - y;
    SetColor(gSelectedPen->color);
    CGContextSetLineWidth(gCanvas, gSelectedPen->width);
    // Для этих практик рисуем контур, как прежняя версия прослойки.
    CGContextStrokeEllipseInRect(gCanvas, CGRectMake(x, y, width, height));
    [gView setNeedsDisplay:YES];
    if (gDebug)
        std::fprintf(stderr, "[gdi] Ellipse left=%d top=%d right=%d bottom=%d color=%06x width=%d\n",
                     left, top, right, bottom, gSelectedPen->color, gSelectedPen->width);
    return TRUE;
}

int DeleteObject(HGDIOBJ obj)
{
    PenData *pen = static_cast<PenData *>(obj);
    if (pen == &gDefaultPen) return TRUE;
    if (gPens.erase(pen) == 0) return FALSE;
    // Старые Circle::Show/Hide удаляют выбранное перо без восстановления.
    // Принимаем этот учебный код и возвращаем безопасное стандартное перо.
    if (gSelectedPen == pen) gSelectedPen = &gDefaultPen;
    delete pen;
    return TRUE;
}

} // extern "C"
