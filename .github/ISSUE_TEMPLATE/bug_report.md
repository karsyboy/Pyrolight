---
name: Bug report
about: Report a reproducible Moonlight Qt PyroWave problem
---

Before reporting a general streaming issue, review the upstream
[Moonlight troubleshooting guide](https://github.com/moonlight-stream/moonlight-docs/wiki/Troubleshooting).

## Problem

Describe the failure and what you expected to happen.

## Steps to reproduce

List the smallest reliable sequence that triggers the problem. State whether
the problem also occurs with H.264, HEVC, or AV1 selected instead of PyroWave.

## Client

- Moonlight Qt PyroWave release/tag:
- Release asset used (Windows installer, Windows portable, or Linux AppImage):
- Operating system:
- GPU and driver:
- Display server/compositor on Linux:
- Resolution, frame rate, bitrate, HDR, and YUV 4:4:4 settings:

## Host

- Pyroshine release/commit:
- Operating system:
- GPU and driver:
- PyroWave encoder settings:

## Logs and screenshots

Attach the client log and relevant Pyroshine log. On Windows, client logs are in
`%TEMP%`; on Linux, launch the AppImage from a terminal and capture its output.
Remove credentials, host addresses, and other sensitive information first.

Add screenshots or short recordings when they help show the issue.
