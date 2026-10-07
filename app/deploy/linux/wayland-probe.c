/*
 * wayland-probe: loadability oracle for the AppImage AppRun.
 *
 * libwayland-client belongs to the host graphics stack: the host's Mesa EGL
 * and Vulkan WSI drivers are linked against the host copy and may need symbols
 * newer than the build environment's, so the AppImage never bundles it in
 * usr/lib. Moonlight links it directly (for Wayland detection, frame pacing and
 * VA-API on Wayland), so it must still be loadable on X11-only hosts.
 *
 * This program embeds the same DT_NEEDED entry and symbol references as
 * Moonlight and the staged libva-wayland without ever calling into them.
 * libwayland has no symbol versions, so the probe is linked with -z now and
 * references the symbols by address: either the dynamic loader resolves all of
 * them and main() runs (the host libwayland-client is usable), or the exec
 * fails and AppRun exposes the build-environment copy staged next to this
 * binary. That copy is only used when the host has none (or one older than the
 * build environment), so it cannot shadow a newer host library.
 *
 * scripts/build-appimage.sh verifies at build time that this file references
 * every libwayland-client symbol imported by those binaries.
 */

#include <stddef.h>

#include <wayland-client.h>

/* volatile keeps the table (and its relocations) alive through -O2. */
static const void *volatile requirements[] = {
    (const void *)wl_display_connect,
    (const void *)wl_display_disconnect,
    (const void *)wl_display_flush,
    (const void *)wl_display_roundtrip,
    (const void *)wl_display_roundtrip_queue,
    (const void *)wl_display_dispatch_queue,
    (const void *)wl_display_get_error,
    (const void *)wl_display_create_queue,
    (const void *)wl_event_queue_destroy,
    (const void *)wl_proxy_add_listener,
    (const void *)wl_proxy_create_wrapper,
    (const void *)wl_proxy_wrapper_destroy,
    (const void *)wl_proxy_destroy,
    (const void *)wl_proxy_get_version,
    (const void *)wl_proxy_marshal_flags, /* libwayland 1.20+ */
    (const void *)wl_proxy_set_queue,
    (const void *)&wl_buffer_interface,
    (const void *)&wl_callback_interface,
    (const void *)&wl_registry_interface,
    (const void *)&wl_surface_interface,
};

int main(void)
{
    return requirements[0] == NULL;
}
