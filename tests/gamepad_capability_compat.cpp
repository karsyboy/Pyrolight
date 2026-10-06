#include "streaming/input/gamepadidentity.h"

static_assert(GamepadIdentity::DualSenseEdgeCapability == 0x0200, "Edge capability");
static_assert(GamepadIdentity::subtype(0x045e, 0x02e3) == 0x0400, "Elite capability");
static_assert(GamepadIdentity::subtype(0x045e, 0x0b22) == 0x2400, "Elite 2 capability");
static_assert(GamepadIdentity::subtype(0x28de, 0x1102) == 0x0800, "Steam capability");
static_assert(GamepadIdentity::subtype(0x28de, 0x1205) == 0x1000, "Deck capability");
int main() { return 0; }
