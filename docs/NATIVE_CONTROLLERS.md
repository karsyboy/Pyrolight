# Native controller metadata

Pyrolight identifies supported controller models by exact SDL VID/PID and sends
subtype metadata in Moonlight's existing arrival packet. Pyroshine can select
native virtual models in `emulation = "auto"`; explicit host emulation overrides
remain available. Steam recognition requires physical acceptance on the host,
as detailed in [Pyroshine's native controller guide](https://github.com/karsyboy/pyroshine/blob/main/docs/NATIVE_CONTROLLERS.md).

## Detection and transport

| Model | Vendor / product | Family / capability |
| --- | --- | --- |
| Xbox Elite Series 1 | `045e:02e3` | Xbox / `0x0400` |
| Xbox Elite Series 2 USB, Bluetooth, BLE | `045e:0b00`, `0b05`, `0b22` | Xbox / `0x2400` (Elite + Series 2) |
| Classic Steam Controller variants | `28de:1101`, `1102`, `1105`, `1106`, `1142` | Steam / `0x0800` |
| Steam Deck built-in controller | `28de:1205` | Steam / `0x1000` |
| DualSense Edge | `054c:0df2` | PlayStation / `0x0200` |

`GamepadIdentity::subtype()` contains this allocation-free detection policy.
The family is corrected for recognized subtypes even if SDL reports unknown.
The GIP keyboard PID `0b02`, Steam Virtual Gamepad `11ff`, newer Valve models,
and unrelated controllers with paddles receive no native subtype. Exact Microsoft
identities follow [SDL USB IDs](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/usb_ids.h);
Valve identities follow [SDL's controller list](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/controller_list.h).

The protocol constants are defined in common-c's `src/Limelight.h`. Build the
client together with its matching protocol fork. Arrival size and byte order do
not change. A new client connected to an older host sends the same family and
ordinary controls; the host can ignore subtype bits. Older clients remain
compatible with new hosts and select generic fallback models. The four standard
Moonlight paddle bits are unchanged and never infer hardware identity.

## SDL input availability

SDL normalized PADDLE1/2/3/4 map directly to Moonlight paddle flags. Elite and
Deck use right upper, left upper, right lower, left lower. Classic uses right
and left grips in the first two slots. Edge retains right rear, left rear,
right Fn, left Fn. Custom mappings and GameControllerDB updates can omit these
controls. SDL also suppresses independent Elite events for some mapped onboard
profiles; test an unmapped hardware profile.

The streaming input handler enables the classic Steam HIDAPI default before
joystick initialization. SDL environment hints retain higher priority. Discovery,
open/close, supported buttons, axes, sensors, battery and feedback continue through
SDL. No second HID polling path is added. Sensor delivery is enabled by host
requests and gyro values travel in degrees/s; accelerometer values remain m/s².

The Linux AppImage pins SDL `release-3.4.18` through sdl2-compat
`release-2.32.74`. Native SDL2 and other builds depend on their installed runtime.
Audited native SDL2 Steam and Deck HIDAPI paths expose rear controls and sensors,
with classic pad-derived D-pad and right axes. They do not expose controller
trackpad contacts. The pinned SDL3 Deck driver exposes two single-finger touchpads
and simple rumble through sdl2-compat. The pinned classic Steam driver exposes
neither controller touch contacts nor standard rumble. Capabilities are queried
from each opened SDL controller; no touch support is fabricated by identity.

The controller touch handler already sends both pad indices in existing packets.
Pyroshine now reads that field. The SDL2 API exposes QAM as MISC1 and, when mapped,
the right Deck pad click as TOUCHPAD. Separate left pad click and capacitive stick
touch misc slots do not fit the currently used SDL2 controller API. Full Deck or
classic touchpad parity is not claimed. Classic firmware pulse haptics, calibration
and orientation quaternion transport are unavailable.

Authoritative runtime references:

- [SDL2 controller normalization](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/SDL_gamecontroller.c)
- [SDL2 Xbox HIDAPI/profile handling](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/hidapi/SDL_hidapi_xboxone.c)
- [SDL2 classic Steam HIDAPI](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/hidapi/SDL_hidapi_steam.c)
- [SDL2 Deck HIDAPI](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/hidapi/SDL_hidapi_steamdeck.c)
- [Pinned SDL3 Deck HIDAPI](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/joystick/hidapi/SDL_hidapi_steamdeck.c)
- [Pinned SDL3 classic HIDAPI](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/joystick/hidapi/SDL_hidapi_steam.c)

## Validation

Run the CMake suites in [CONTRIBUTING](../CONTRIBUTING.md#validation), including
common-c's `CONTROLLER_PROTOCOL_TESTS`. Identity tests cover transport VID/PID
variants, ordinary Sony/Microsoft devices, unknown Valve hardware and unrelated
paddle devices. SDL virtual fixtures exercise all combinations of two/four
normalized paddles, press/release and ordinary-button isolation. These fixtures
are not physical firmware/USB/Bluetooth tests.

Run the host's capability comparison script against this checkout's common-c
header. Rebuild the application and common-c together. Before releasing, use the
host guide's physical acceptance matrix for Elite Series 2, classic Steam and an
actual Deck client, plus standard Xbox, DualSense/Edge and Nintendo regression.
Record SDL runtime, mapping, profile, transport and arrival metadata. Check
single/multiple controllers, replacement, ownership change and reconnect while
rear controls are held. Explicit host Xbox must select the generic model.
