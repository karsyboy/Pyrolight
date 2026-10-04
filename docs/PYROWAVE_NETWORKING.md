# PyroWave networking and compatibility

PyroWave sends every frame as a complete intra frame, so high bitrates produce
large bursts of UDP packets. This guide covers the receive-buffer and bandwidth
tools in Settings, then the transport dialects and host compatibility rules.
For the decoder architecture see [PyroWave integration](PYROWAVE.md).

## Receive buffers

Undersized socket receive buffers cause burst packet loss at PyroWave bitrates.
For PyroWave, `moonlight-common-c` requests `(negotiatedPacketSize + 16) * 8192`
bytes of receive buffer (8192 packets); H.264, HEVC and AV1 keep 2048 packets.
A 1392-byte packet requests 11,534,336 bytes. One setup log line reports the
requested, returned and effective sizes; Linux returns doubled `SO_RCVBUF`
accounting, so clamping logs compare half the returned value with the request.

### Linux

Settings reads `/proc/sys/net/core/rmem_max` when opened and on demand, using
the configured packet size (1392 by default). An adequate limit shows OK; an
unreadable one stays unknown. A small limit shows **Fix it**, **Copy command**
and **Recheck**, and launching a PyroWave stream logs a warning. Nothing
privileged happens until you press **Fix it**. The recommended setting is:

```sh
sudo sysctl -w net.core.rmem_max=33554432 && \
echo 'net.core.rmem_max = 33554432' | \
sudo tee /etc/sysctl.d/60-moonlight-pyrowave.conf
```

**Fix it** runs this through `pkexec`, using Distrobox/host bridges where
available. In Flatpak, or without the needed helpers, use **Copy command** and
run it yourself. In Steam Gaming Mode without an authorization agent the fix
fails with a message; switch to Desktop Mode and run it from Konsole. No helper
is installed silently.

### Windows

Settings inspects only the routed physical Ethernet adapter and the
driver-declared bounds for `*ReceiveBuffers`, `ReceiveBufferLen` and
`PendingReceives`; absent or unknown properties are skipped. Undersized values
can be raised to a value the driver accepts, at most 2048, after UAC approval.
The adapter is targeted by GUID, so other adapters are untouched. Applying the
change restarts the adapter and may need a reboot. Failures remain diagnostics
and never block streaming.

## Bandwidth calibration

In Settings, select an online paired PyroWave host and press **Calibrate network
bandwidth** while no stream is running. The client runs one 32 MiB warm-up and
three measured 32 MiB HTTPS downloads from the host's authenticated
`/pyrowave-bandwidth-probe`, then recommends a bitrate ceiling:

- the slowest measured throughput,
- limited by the routed physical host and client Ethernet speeds when known,
- multiplied by 80%.

A healthy 1 GbE path typically recommends about 750–800 Mbps. The route type
(Ethernet, Wi-Fi or unknown) is reported; nominal Wi-Fi speeds and virtual
adapters are not guessed. **Use recommended ceiling** applies the result; it
never changes settings automatically. The 2,000,000 Kbps codec limit and the
per-frame budget still apply.

Calibration measures TCP throughput only. It does not establish loss-free UDP
delivery or GPU decode capacity.

Implementation contract: the probe runs on a worker thread, pins the paired
host certificate, presents the normal client certificate, buffers at most
64 KiB, and rejects redirects, compression, oversized or truncated responses
and timeouts (10 s per request). **Stop** interrupts an active probe. Host-side
behavior is specified in Pyroshine's
[PyroWave compatibility](https://github.com/karsyboy/pyroshine/blob/main/docs/PYROWAVE_COMPATIBILITY.md#authenticated-calibration) guide.

## Transport dialects

Setup chooses one dialect per RTSP handshake, and the decoder records it at
initialization:

| Dialect | Hosts | Frame path |
| --- | --- | --- |
| Native wire-v1 | Pyroshine | Complete contiguous frames through ordinary GameStream reassembly, strict native validation, the mailbox and the GPU decoder |
| Record-framed | Vibepollo (advertising `rtpmap:99 PYROWAVE/90000` and bitstream `186f0393`) | Validated records through `pyrowaveframing.cpp`, with recovery of partial frames |

Native version 1 wins when a host advertises both. Native frames never pass
through the record adapter and are never inspected to infer another framing.
Unknown revisions and contradictory markers fail with a compatibility message.
Record recovery and padding/color normalization are isolated to explicitly
negotiated record sessions. Legacy length-prefixed payloads are rejected,
including on reconnect. Automatic codec selection still uses the conventional
codecs.

HTTP HDR capabilities are translated at setup: native HDR is the orthogonal
`0x02000000` bit; record HDR 4:2:0 uses that bit and HDR 4:4:4 uses
`0x04000000`. After setup, decoder and renderer configuration use local chroma,
depth and range values. Nonary-derived hosts leave default sequence-header color
bits, which are normalized from SDP only in record mode.

The common-c side of these rules, including record critical-prefix metadata in
`DECODE_UNIT`, is documented in the submodule's `docs/pyrowave.md`. Build the
application and common-c together.

## Tests and validation limits

The [client unit tests](../CONTRIBUTING.md#validation) cover profile math,
strict native frames, record parsing and recovery, receive-limit status, and
golden bitstreams from both forks. Common-c's protocol suite covers SDP,
native/record isolation, packet counts, loss and reordering. The optional
`pyrowave-https` harness checks TLS discovery, transfers, an invalid pin,
compression, redirects, size and timeout handling with temporary certificates.

These fixtures do not establish that a live Vibepollo or Nonary session works.
Test real HDR output, sustained bitrate, NIC settings and in-process reconnects
on the deployment hardware before recording those cases as working.
