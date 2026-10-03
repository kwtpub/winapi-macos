#include "../gdi_compat.mm"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

static_assert(sizeof(COLORREF) == 4, "COLORREF must match the Windows ABI");
static_assert(sizeof(DWORD) == 4, "DWORD must match the Windows ABI");
static_assert(sizeof(WORD) == 2, "WORD must match the Windows ABI");
static_assert(sizeof(BYTE) == 1, "BYTE must match the Windows ABI");

static int failures = 0;

static void Check(bool condition, const char *description)
{
    std::fprintf(condition ? stdout : stderr, "%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
}

static NSBitmapImageRep *Snapshot()
{
    NSBitmapImageRep *rep = [gView bitmapImageRepForCachingDisplayInRect:gView.bounds];
    [gView cacheDisplayInRect:gView.bounds toBitmapImageRep:rep];
    return rep;
}

static bool IsColorAt(NSBitmapImageRep *rep, int x, int y, COLORREF expected)
{
    const double scaleX = rep.pixelsWide / gView.bounds.size.width;
    const double scaleY = rep.pixelsHigh / gView.bounds.size.height;
    // Compare the captured raster's native RGB component values. Cocoa's
    // cache bitmap labels its data NSCalibratedRGBColorSpace; converting that
    // NSColor to sRGB changes primary component values and gives false failures
    // despite the actual PNG byte values matching the requested COLORREF.
    NSColor *color = [rep colorAtX:static_cast<NSInteger>((x + 0.5) * scaleX)
                                y:static_cast<NSInteger>((y + 0.5) * scaleY)];
    if (!color) return false;
    const double tolerance = 2.0 / 255.0;
    return std::fabs(color.redComponent - (expected & 0xff) / 255.0) <= tolerance
        && std::fabs(color.greenComponent - ((expected >> 8) & 0xff) / 255.0) <= tolerance
        && std::fabs(color.blueComponent - ((expected >> 16) & 0xff) / 255.0) <= tolerance;
}

static bool IsRegionColor(NSBitmapImageRep *rep, int left, int top, int right, int bottom, COLORREF color)
{
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (!IsColorAt(rep, x, y, color)) return false;
    return true;
}

static NSEvent *KeyEvent(NSEventType type, unsigned short code, NSString *characters,
                        NSEventModifierFlags modifiers = 0)
{
    return [NSEvent keyEventWithType:type
                          location:NSZeroPoint
                     modifierFlags:modifiers
                         timestamp:0
                      windowNumber:gWindow.windowNumber
                           context:nil
                        characters:characters
       charactersIgnoringModifiers:characters
                         isARepeat:NO
                           keyCode:code];
}

static void TestKey(int vk, unsigned short code, NSString *characters, const char *description)
{
    [(GDICompatView *)gView keyDown:KeyEvent(NSEventTypeKeyDown, code, characters)];
    const short first = GetAsyncKeyState(vk);
    Check((static_cast<unsigned short>(first) & 0x8000) != 0, description);
    Check((first & 1) != 0, "first poll includes the key-pressed low bit");
    const short second = GetAsyncKeyState(vk);
    Check((static_cast<unsigned short>(second) & 0x8000) != 0 && (second & 1) == 0,
          "held key stays down and its low bit is consumed");
    [(GDICompatView *)gView keyUp:KeyEvent(NSEventTypeKeyUp, code, characters)];
    Check((static_cast<unsigned short>(GetAsyncKeyState(vk)) & 0x8000) == 0,
          "key-up clears the down state");
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        unsetenv("GDI_AUTOPLAY");
        HWND window = GetConsoleWindow();
        HDC dc = GetWindowDC(window);
        GetAsyncKeyState(-1); // Drain initial activation events before key assertions.

        if (argc > 1 && std::string(argv[1]) == "--close") {
            [gWindow performClose:nil];
            GetAsyncKeyState('1');
            std::fputs("FAIL: closing the window did not stop polling\n", stderr);
            return 77;
        }

        Check(window != nullptr && dc != nullptr, "window and device context are available");
        Check(RGB(0x12, 0x34, 0x56) == 0x00563412u, "RGB uses the Windows COLORREF byte order");
        Check(GetWindowDC(nullptr) == nullptr, "invalid window handle is rejected");
        Check(GetAsyncKeyState(-1) == 0 && GetAsyncKeyState(256) == 0,
              "invalid virtual keys are rejected");

        const COLORREF blue = RGB(0, 0, 255);
        const COLORREF red = RGB(255, 0, 0);
        Check(IsColorAt(Snapshot(), 5, 5, blue), "canvas background matches the exercise Hide color");
        Check(SetPixel(dc, 13, 29, red) == red, "SetPixel reports the actual color");
        NSBitmapImageRep *point = Snapshot();
        Check(IsColorAt(point, 13, 29, red), "GDI point appears at the view top-left coordinates");
        Check(IsColorAt(point, 13, kH - 1 - 29, blue), "GDI point is not reflected vertically");
        SetPixel(dc, 13, 29, blue);
        Check(IsColorAt(Snapshot(), 13, 29, blue), "drawing the Hide color erases a point");

        HPEN redPen = CreatePen(PS_SOLID, 2, red);
        HGDIOBJ stockPen = SelectObject(dc, redPen);
        Check(redPen != nullptr && stockPen != nullptr, "selecting a pen returns a usable stock pen");
        Check(Ellipse(dc, 50, 80, 100, 140) != 0, "ellipse is drawn with a selected pen");
        Check(!IsRegionColor(Snapshot(), 47, 77, 104, 144, blue), "red ellipse produces visible pixels");
        Check(SelectObject(dc, stockPen) == redPen, "previous pen can be restored safely");
        Check(DeleteObject(redPen) != 0, "deselected dynamic pen can be deleted");
        Check(DeleteObject(redPen) == 0, "duplicate deletion is rejected");
        Check(SelectObject(dc, reinterpret_cast<HGDIOBJ>(0x123)) == nullptr,
              "unknown GDI object is rejected");
        Check(DeleteObject(reinterpret_cast<HGDIOBJ>(0x123)) == 0,
              "unknown GDI object cannot be freed");
        Check(Ellipse(dc, 170, 80, 210, 120) != 0,
              "restored stock pen remains valid after other pen deletion");

        HPEN bluePen = CreatePen(PS_SOLID, 2, blue);
        stockPen = SelectObject(dc, bluePen);
        Ellipse(dc, 50, 80, 100, 140);
        Check(IsRegionColor(Snapshot(), 47, 77, 104, 144, blue),
              "Hide erases the entire ellipse without red antialiasing edges");
        SelectObject(dc, stockPen);
        DeleteObject(bluePen);

        HPEN temporaryPen = CreatePen(PS_SOLID, 2, red);
        SelectObject(dc, temporaryPen);
        Check(DeleteObject(temporaryPen) != 0, "legacy deletion of a selected pen is supported");
        Check(Ellipse(dc, 230, 80, 270, 120) != 0,
              "drawing after legacy pen deletion falls back to a valid stock pen");

        TestKey('1', 18, @"1", "number-row digit maps to Windows VK_1");
        TestKey('A', 0, @"ф", "physical A key maps to VK_A under the Russian layout");
        TestKey(0x25, 123, @"\uf702", "left arrow maps to Windows VK_LEFT");

        [(GDICompatView *)gView keyDown:KeyEvent(NSEventTypeKeyDown, 18, @"1")];
        [(GDICompatView *)gView keyUp:KeyEvent(NSEventTypeKeyUp, 18, @"1")];
        const short quick = GetAsyncKeyState('1');
        Check((quick & 1) != 0 && (static_cast<unsigned short>(quick) & 0x8000) == 0,
              "a quick completed press is recorded without pretending the key remains held");

        [(GDICompatView *)gView flagsChanged:KeyEvent(NSEventTypeFlagsChanged, 56, @"", NSEventModifierFlagShift)];
        Check((static_cast<unsigned short>(GetAsyncKeyState(0x10)) & 0x8000) != 0,
              "Shift modifier maps to Windows VK_SHIFT");
        [(GDICompatView *)gView flagsChanged:KeyEvent(NSEventTypeFlagsChanged, 56, @"")];
        Check((static_cast<unsigned short>(GetAsyncKeyState(0x10)) & 0x8000) == 0,
              "releasing Shift clears its state");

        [(GDICompatView *)gView keyDown:KeyEvent(NSEventTypeKeyDown, 18, @"1")];
        [gWindow resignKeyWindow];
        Check(GetAsyncKeyState('1') == 0, "losing window focus clears held and pressed key states");

        NSData *png = [Snapshot() representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        [png writeToFile:@"/tmp/exoop-gdi-smoke.png" atomically:YES];
        std::printf("%d failure(s); captured view: /tmp/exoop-gdi-smoke.png\n", failures);
        return failures ? 1 : 0;
    }
}
