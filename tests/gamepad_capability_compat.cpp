#include <Limelight.h>

// Exercise both header versions regardless of the developer's submodule edits.
#undef LI_CCAP_DUALSENSE_EDGE
#ifdef TEST_PROTOCOL_HAS_EDGE
#define LI_CCAP_DUALSENSE_EDGE 0x0200
#endif

#include "streaming/input/gamepadidentity.h"

static_assert(GamepadIdentity::DualSenseEdgeCapability == 0x0200,
              "Old and new protocol headers must advertise the same Edge bit");

int main()
{
    return 0;
}
