#include "displaybackend.h"
#include "utils.h"

#include <QGuiApplication>
#include <QLibraryInfo>
#include <QVersionNumber>
#include <QtDebug>

#ifdef HAS_X11
#include <X11/Xlib.h>
#endif

namespace {

// Whether the X server that Qt's xcb plugin uses ($DISPLAY) is Xwayland.
bool isXServerXWayland()
{
#ifdef HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (display != nullptr) {
        // Xwayland 21.1+ advertises the XWAYLAND extension (the same check SDL uses)
        int opcode, event, error;
        bool xwayland = XQueryExtension(display, "XWAYLAND", &opcode, &event, &error);
        XCloseDisplay(display);
        if (xwayland) {
            return true;
        }
    }
#endif

    // Older Xwayland releases lack the extension. An X server running next
    // to a reachable Wayland compositor is Xwayland in practice (this is the
    // upstream heuristic).
    return WMUtils::isRunningWayland();
}

}

void DisplayBackend::preferNativeWayland()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    QVersionNumber qtVersion = QLibraryInfo::version();
    const char* platforms = preferredQtPlatforms(qtVersion.majorVersion(), qtVersion.minorVersion(),
                                                 !qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"),
                                                 qEnvironmentVariableIsSet("WAYLAND_DISPLAY"),
                                                 qgetenv("XDG_SESSION_TYPE") == "wayland",
                                                 qEnvironmentVariableIsSet("DISPLAY"));
    if (platforms != nullptr) {
        qInfo().noquote() << QStringLiteral("Preferring native Wayland over the Qt %1 default (QT_QPA_PLATFORM=%2). Set QT_QPA_PLATFORM to override this.")
                             .arg(qtVersion.toString(), platforms);
        qputenv("QT_QPA_PLATFORM", platforms);
    }
#endif
}

DisplayBackend::Kind DisplayBackend::current()
{
    static const Kind kind = []() {
        QByteArray platform = QGuiApplication::platformName().toUtf8();
        return classify(platform.constData(), platform == "xcb" && isXServerXWayland());
    }();
    return kind;
}
