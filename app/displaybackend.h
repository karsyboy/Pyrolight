#pragma once

#include <cstring>

// Linux display backend (window-system path) used by Pyrolight. Qt selects the
// platform plugin first; SDL is then forced onto the matching video driver, so
// the Qt platform actually in use is the authoritative input. The same
// classification drives SDL driver selection, startup logging and the Settings
// diagnostic. The inline functions are pure so tests/display_backend.cpp can
// cover them without a display.
namespace DisplayBackend {

enum class Kind { NativeWayland, X11, XWayland, Kms, Other };

// QT_QPA_PLATFORM value that makes Qt prefer native Wayland, or nullptr to keep
// Qt's own choice. Qt 6.3+ already tries "wayland" before "xcb" whenever a
// Wayland session is detected; Qt 6.0-6.2 (the AppImage ships 6.2) puts xcb
// first on GNOME. The semicolon list keeps Qt's fallback: if the Wayland
// plugin cannot load or connect, Qt continues with xcb.
inline const char* preferredQtPlatforms(int qtMajor, int qtMinor, bool qpaPlatformSet,
                                        bool hasWaylandDisplay, bool isWaylandSessionType,
                                        bool hasX11Display)
{
    if (qpaPlatformSet || qtMajor != 6 || qtMinor >= 3 ||
            !(hasWaylandDisplay || isWaylandSessionType)) {
        return nullptr;
    }
    return hasX11Display ? "wayland;xcb" : "wayland";
}

// xServerIsXWayland describes the X server Qt's xcb plugin is connected to and
// is only consulted for the xcb platform.
inline Kind classify(const char* qtPlatform, bool xServerIsXWayland)
{
    if (strncmp(qtPlatform, "wayland", 7) == 0) {
        return Kind::NativeWayland;
    }
    else if (strcmp(qtPlatform, "xcb") == 0) {
        return xServerIsXWayland ? Kind::XWayland : Kind::X11;
    }
    else if (strcmp(qtPlatform, "eglfs") == 0 || strcmp(qtPlatform, "linuxfb") == 0) {
        return Kind::Kms;
    }
    return Kind::Other;
}

// SDL video driver that matches Qt's window system, or nullptr to let SDL choose.
inline const char* sdlVideoDriver(Kind kind)
{
    switch (kind) {
    case Kind::NativeWayland:
        return "wayland";
    case Kind::X11:
    case Kind::XWayland:
        return "x11";
    case Kind::Kms:
        return "kmsdrm";
    default:
        return nullptr;
    }
}

// User-visible name, or nullptr where the concept does not apply (Windows, macOS).
inline const char* displayName(Kind kind)
{
    switch (kind) {
    case Kind::NativeWayland:
        return "Native Wayland";
    case Kind::X11:
        return "X11";
    case Kind::XWayland:
        return "XWayland";
    case Kind::Kms:
        return "KMSDRM";
    default:
        return nullptr;
    }
}

// Applies preferredQtPlatforms() to the environment. Call before QGuiApplication.
void preferNativeWayland();

// Classifies the running QGuiApplication. The result cannot change afterwards.
Kind current();

}
