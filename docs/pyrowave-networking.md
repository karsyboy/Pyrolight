# PyroWave networking and compatibility

Native wire-v1 remains the preferred and sole native frame path. Setup chooses
its dialect once; the decoder saves that choice when initialized. Complete
contiguous frames use ordinary GameStream reassembly, strict native validation,
the existing mailbox and GPU decoder. Native frames are never passed through a
record adapter or inspected to infer another transport.

Vibepollo hosts advertising `rtpmap:99 PYROWAVE/90000` and the verified
`x-ss-pyrowave.bitstream:186f0393` family can select record compatibility.
Native version 1 wins when both dialects are advertised. Unknown revisions and
contradictory markers fail with a compatibility message. Automatic codec
selection retains conventional codecs. Record recovery and padding/color
normalization are isolated to explicitly negotiated record sessions. Legacy
length-prefixed payloads are rejected, including on reconnect.

HTTP HDR capabilities are translated at setup: native HDR is orthogonal
`0x02000000`; record HDR420 uses that bit and HDR444 uses `0x04000000`.
Downstream decoder/renderer configuration uses local chroma, depth and range
axes. Nonary's default sequence-header color bits are normalized from SDP only
in record mode. Full-range SDR8 and HDR10 are the verified Pyroshine profiles.

## Receive buffering

Common-c requests `(negotiatedPacketSize + 16) * 8192` bytes for PyroWave,
preserving 2048 packets for H.264, HEVC and AV1. A 1392-byte packet requests
11,534,336 bytes; a 1024-byte packet requests 8,519,680 bytes. Linux's returned
SO_RCVBUF is doubled accounting, so clamping logs compare half that value with
the request. One setup log includes requested, returned and effective sizes.

Settings read `/proc/sys/net/core/rmem_max` on demand and when opened, using the
configured packet size (1392 by default). Adequate limits show OK; unreadable
limits remain unknown. Small limits explain burst loss and expose **Fix it**,
**Copy command**, and **Recheck**. Stream launch emits a PyroWave-only warning.
No privileged change happens until the user presses Fix it.

```sh
sudo sysctl -w net.core.rmem_max=33554432 && \
echo 'net.core.rmem_max = 33554432' | \
sudo tee /etc/sysctl.d/60-moonlight-pyrowave.conf
```

Automatic Linux remediation uses pkexec. Distrobox/host bridges are used where
available; Flatpak and missing helpers retain the manual command. Gaming Mode
without an authorization agent reports the failure and directs the user to
Desktop Mode/Konsole. No helper is silently installed.

Windows diagnostics inspect only the routed physical Ethernet adapter and the
driver-declared numeric bounds for `*ReceiveBuffers`, `ReceiveBufferLen`, and
`PendingReceives`. Absent/unknown properties are skipped. Undersized settings
can be raised to a representable driver value, bounded at 2048, after UAC
authorization. The adapter is targeted by GUID; unrelated settings remain
untouched. The UI warns that the adapter will restart and reboot may be needed.
Failure remains a diagnostic and never blocks streaming. Windows runtime and
UAC behavior require validation on actual NIC drivers.

## Explicit network calibration

Select an online paired PyroWave host in Settings and press **Calibrate network
bandwidth** after ending any stream. Support is discovered over HTTPS. One
32 MiB warm-up and three 32 MiB measured transfers run on a worker. The
downloader pins the paired host certificate, uses the normal client certificate,
buffers only 64 KiB, and rejects redirects, compressed/oversized/truncated
responses and timeouts. Stop interrupts an active probe; each request has a
10-second deadline.

The slowest measured throughput is limited by the routed physical host/client
Ethernet capacities when known, then multiplied by 80%. Ethernet/Wi-Fi/unknown
route type is reported; nominal Wi-Fi capacity and virtual adapters are not
guessed. A healthy 1 GbE path can recommend about 750–800 Mbps. The existing
2,000,000 Kbps codec ceiling and per-frame transport budget remain applicable.

**Use recommended ceiling** is explicit. HTTPS throughput does not establish
loss-free UDP behavior or GPU/decode performance. This feature does not add a
GPU calibration subsystem or automatically change streaming settings.

## Tests

`cmake -S tests -B build/tests`, build and CTest exercise profile math, strict
native frames, record parsing/recovery, receive-limit status and real GPU golden
bitstreams from both forks. Common-c's separate protocol suite covers SDP and
native/record isolation, packet counts, loss and reordering.

When Qt5, OpenSSL and a built common-c library are available, the test CMake
project also builds `pyrowave-https`. Run
`python3 tests/pyrowave_https.py build/tests/pyrowave-https` for local TLS
discovery, successful transfers, invalid pin, compression, redirect, size and
timeout checks. It generates temporary certificates and isolated settings;
Python, openssl and loopback socket access are required.

The fixtures do not establish that a live Vibepollo/Nonary display session works.
Test real HDR output, sustained bitrate, NIC settings and in-process reconnects
on the deployment hardware before recording those matrix cells as working.
