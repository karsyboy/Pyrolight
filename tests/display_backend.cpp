#include "../app/displaybackend.h"
#include <cassert>
#include <cstring>
static bool eq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }
int main() {
    using namespace DisplayBackend;
    // Qt 6.0-6.2 prefers xcb on GNOME: ask for Wayland first, keep xcb as fallback.
    assert(eq(preferredQtPlatforms(6, 2, false, true, true, true), "wayland;xcb"));
    assert(eq(preferredQtPlatforms(6, 2, false, false, true, true), "wayland;xcb"));
    assert(eq(preferredQtPlatforms(6, 2, false, true, false, false), "wayland"));
    // No Wayland session, user override, Qt 6.3+ (already Wayland first) or Qt 5: keep Qt's choice.
    assert(!preferredQtPlatforms(6, 2, false, false, false, true));
    assert(!preferredQtPlatforms(6, 2, true, true, true, true));
    assert(!preferredQtPlatforms(6, 3, false, true, true, true));
    assert(!preferredQtPlatforms(6, 11, false, true, true, true));
    assert(!preferredQtPlatforms(5, 15, false, true, true, true));
    // The Qt platform actually in use decides; xcb is split by the X server's identity.
    assert(classify("wayland", false) == Kind::NativeWayland);
    assert(classify("wayland", true) == Kind::NativeWayland);
    assert(classify("wayland-egl", false) == Kind::NativeWayland);
    assert(classify("xcb", false) == Kind::X11);
    assert(classify("xcb", true) == Kind::XWayland);
    assert(classify("eglfs", false) == Kind::Kms);
    assert(classify("linuxfb", false) == Kind::Kms);
    assert(classify("windows", false) == Kind::Other);
    assert(classify("cocoa", true) == Kind::Other);
    assert(classify("offscreen", false) == Kind::Other);
    // SDL always follows Qt: no mixed Wayland/X11 configuration.
    assert(eq(sdlVideoDriver(Kind::NativeWayland), "wayland"));
    assert(eq(sdlVideoDriver(Kind::X11), "x11"));
    assert(eq(sdlVideoDriver(Kind::XWayland), "x11"));
    assert(eq(sdlVideoDriver(Kind::Kms), "kmsdrm"));
    assert(!sdlVideoDriver(Kind::Other));
    // Settings diagnostic names; hidden where the concept does not apply.
    assert(eq(displayName(Kind::NativeWayland), "Native Wayland"));
    assert(eq(displayName(Kind::X11), "X11"));
    assert(eq(displayName(Kind::XWayland), "XWayland"));
    assert(eq(displayName(Kind::Kms), "KMSDRM"));
    assert(!displayName(Kind::Other));
}
