# Pyrolight

<p align="center"><img src="assets/icon.png" alt="Pyrolight" width="160"></p>

<p>
    <a href="https://github.com/karsyboy/pyrolight/actions/workflows/release.yml">
        <img src="https://github.com/karsyboy/pyrolight/actions/workflows/release.yml/badge.svg"></a>
</p>

Pyrolight is a game-streaming client based on
[Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt). It keeps
Moonlight's standard codecs and features and adds an explicitly selected
[PyroWave](https://github.com/karsyboy/pyrowave) video path for
[Pyroshine](https://github.com/karsyboy/pyroshine) hosts.

- PyroWave decoding on the GPU in 4:2:0 and 4:4:4, SDR and HDR10.
- H.264, HEVC, and AV1 with any Moonlight-compatible host.
- PyroWave network calibration and receive-buffer diagnostics.
- Named streaming profiles for switching between sets of stream settings.
- Native Wayland on Linux, with automatic fallback to X11/XWayland. **Settings →
  Advanced Settings → Display backend** shows which one is in use; see
  [Linux display backends](docs/LINUX_DISPLAY.md).
- Native model metadata for Xbox Elite, classic Steam Controller, Steam Deck and DualSense Edge on matching Pyroshine hosts; see [controller support and limits](docs/NATIVE_CONTROLLERS.md).

PyroWave is an intra-only wavelet codec with very low GPU encode and decode
latency, designed for high-bandwidth local networks. It is never selected
automatically.

## Requirements

- Windows 10 or later (x64), or Linux (x86_64).
- For PyroWave: a hardware Vulkan 1.3 GPU and driver that pass the client's
  startup decoder probe, a [Pyroshine](https://github.com/karsyboy/pyroshine)
  host whose `pyroshine healthcheck` reports PyroWave support, and a network
  that sustains the selected bitrate. Software Vulkan devices are rejected.

Standard codecs work with Pyroshine, Sunshine, and other hosts supported by
upstream Moonlight Qt.

## Installation

Download the file for your system from the
[releases page](https://github.com/karsyboy/pyrolight/releases) and install it:

| System | Download | Install |
| --- | --- | --- |
| Windows | `*-windows-x64-installer.exe` | Run the installer |
| Windows (portable) | `*-windows-x64-portable.zip` | Extract and run `Pyrolight.exe` |
| Arch Linux / CachyOS | `pyrolight-*.pkg.tar.zst` | `sudo pacman -U ./pyrolight-*.pkg.tar.zst` |
| Debian / Ubuntu | `pyrolight_*.deb` | `sudo apt install ./pyrolight_*.deb` |
| Fedora / RHEL | `pyrolight-*.rpm` | `sudo dnf install ./pyrolight-*.rpm` |
| Other Linux | `*-linux-x86_64.AppImage` | `chmod +x pyrolight-*.AppImage`, then run it |

To upgrade, install the newer release the same way. After installing a Linux
package, start **Pyrolight** from the application menu or run `pyrolight`.

- The Linux packages need Ubuntu 22.04, Debian 12, Fedora, RHEL 10, Arch or
  newer (glibc 2.35). They replace an installed `moonlight-qt`; its settings and
  paired hosts carry over. See [Linux packages](docs/LINUX_PACKAGES.md).
- Each release includes `SHA256SUMS` for checking downloads.
- Pyrolight does not check for updates; watch the releases page. macOS, Steam
  Link, ARM, Snap, and Flatpak builds are not published.

## Streaming with PyroWave

1. Pair with the host in Pyrolight.
2. In **Settings**, set **Video codec** to **PyroWave**. The option appears when
   the client's PyroWave decoder probe succeeds on this GPU.
3. Enable **HDR** or **YUV 4:4:4** only when the host and your display support
   them.
4. Choose a bitrate. PyroWave's bitrate is a per-frame quality budget that scales
   with frame rate; the default is approximately 192 Mbps for 4K60 and 384 Mbps
   for 4K120 in 4:2:0 SDR. Use **Calibrate network bandwidth** to measure a safe
   ceiling for a paired host.

From the command line, pass the same options to the AppImage or `Pyrolight.exe`:

```sh
./pyrolight-v*-linux-x86_64.AppImage stream --video-codec PyroWave --bitrate 200000 \
  --resolution 3840x2160 --fps 60 --hdr --yuv444 HOST Desktop
```

PyroWave accepts up to 2,000,000 Kbps and 240 FPS, provided each encoded frame
budget stays between 1 KiB and 3 MiB. Unsupported host capabilities, devices,
or frame budgets fail with an error instead of falling back to another codec.

## Streaming profiles

The **Streaming Profile** section of Settings creates, duplicates, renames,
deletes, and switches between named profiles. A profile stores stream settings
such as resolution, frame rate, bitrate, codec, HDR, YUV 4:4:4, V-Sync, frame
pacing, audio configuration, window mode, decoder, and renderer. Input, interface,
and language settings stay global. Settings from before profiles existed become
the **Default** profile.

## Troubleshooting

- **Burst packet loss on Linux:** high PyroWave bitrates need larger UDP receive
  buffers. Settings shows the current `net.core.rmem_max` status and offers
  **Fix it** or **Copy command**. See [network tuning](docs/PYROWAVE_NETWORKING.md).
- **Stream limited below the target frame rate:** test with V-Sync disabled so
  the Vulkan renderer can use the lowest-latency present mode. This is a
  diagnostic, not a PyroWave requirement.
- **"The host does not support the selected PyroWave color mode":** the host did
  not advertise PyroWave, or not with the selected HDR/4:4:4 combination. Change
  those settings, or run `pyroshine healthcheck` on the host to check its
  PyroWave support.
- **Linux window or input problems:** check **Display backend** in Settings,
  then start Pyrolight with `QT_QPA_PLATFORM=xcb` (for example
  `QT_QPA_PLATFORM=xcb pyrolight`) to see whether the issue is specific to
  native Wayland.
- **General streaming problems:** see the upstream
  [Moonlight troubleshooting guide](https://github.com/moonlight-stream/moonlight-docs/wiki/Troubleshooting),
  and compare with H.264, HEVC, or AV1 to isolate PyroWave-specific issues.

When reporting a bug, include the release, OS, GPU and driver, host version,
stream settings, and client and host logs. On Windows, client logs are in
`%TEMP%`; on Linux, run the AppImage from a terminal and capture its output.

## Documentation

- [PyroWave network tuning and calibration](docs/PYROWAVE_NETWORKING.md)
- [PyroWave integration architecture](docs/PYROWAVE.md)
- [Native controller metadata and validation](docs/NATIVE_CONTROLLERS.md)
- [Linux display backends and AppImage packaging](docs/LINUX_DISPLAY.md)
- [Linux distribution packages](docs/LINUX_PACKAGES.md)
- [Building, testing, and releasing](CONTRIBUTING.md)
- [Upstream Moonlight documentation](https://github.com/moonlight-stream/moonlight-docs/wiki)

## License and credits

Pyrolight is an independent community fork of
[Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) and is not an
official Moonlight or PyroWave project. It periodically merges upstream client
changes. Its `moonlight-common-c` submodule points to the
[`karsyboy/moonlight-common-c`](https://github.com/karsyboy/moonlight-common-c)
fork, which carries the PyroWave and controller protocol extensions. The codec
is the pinned [`karsyboy/pyrowave`](https://github.com/karsyboy/pyrowave) fork
of [Themaister's PyroWave](https://github.com/Themaister/pyrowave).

Licensed under the [GNU General Public License v3](LICENSE), preserving
Moonlight's copyright and license notices. PyroWave is distributed under its own
MIT license, included with packaged runtime components. Bundled dependencies
retain their respective licenses.
