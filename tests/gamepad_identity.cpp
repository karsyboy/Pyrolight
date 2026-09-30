#ifdef NDEBUG
#undef NDEBUG
#endif
#include "streaming/input/gamepadidentity.h"
#include <SDL.h>
#include <cassert>

int main()
{
    using namespace GamepadIdentity;
    assert(isDualSenseEdge(0x054c, 0x0df2));
    assert(!isDualSenseEdge(0x054c, 0x0ce6));
    assert(!isDualSenseEdge(0x045e, 0x0df2));
    assert(!isDualSenseEdge(0, 0));
    assert(EdgeButtons == 0x000f0000);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE1] == PADDLE1_FLAG);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE2] == PADDLE2_FLAG);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE3] == PADDLE3_FLAG);
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_PADDLE4] == PADDLE4_FLAG);
    const int ordinary[] = { A_FLAG, B_FLAG, X_FLAG, Y_FLAG, BACK_FLAG,
        SPECIAL_FLAG, PLAY_FLAG, LS_CLK_FLAG, RS_CLK_FLAG, LB_FLAG, RB_FLAG,
        UP_FLAG, DOWN_FLAG, LEFT_FLAG, RIGHT_FLAG, MISC_FLAG };
    for (unsigned i = 0; i < sizeof(ordinary) / sizeof(ordinary[0]); ++i) {
        assert(ButtonMap[i] == ordinary[i]);
    }
    assert(ButtonMap[SDL_CONTROLLER_BUTTON_TOUCHPAD] == TOUCHPAD_FLAG);
    assert(DualSenseEdgeCapability == 0x200);

#if SDL_VERSION_ATLEAST(2, 24, 0)
    // Exercise normalized SDL events and advertised buttons without hardware.
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    SDL_VirtualJoystickDesc desc = {};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.nbuttons = 21;
    desc.vendor_id = SonyVendorId;
    desc.product_id = DualSenseEdgeProductId;
    desc.name = "Edge normalization test";
    int device = SDL_JoystickAttachVirtualEx(&desc);
    assert(device >= 0);
    SDL_Joystick* joystick = SDL_JoystickOpen(device);
    assert(joystick);
    char guid[33];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof(guid));
    char mapping[256];
    SDL_snprintf(mapping, sizeof(mapping), "%s,Edge normalization test,a:b0,paddle1:b20,paddle2:b19,paddle3:b18,paddle4:b17,", guid);
    assert(SDL_GameControllerAddMapping(mapping) >= 0);
    SDL_GameController* controller = SDL_GameControllerOpen(device);
    assert(controller);
    assert(isDualSenseEdge(SDL_GameControllerGetVendor(controller), SDL_GameControllerGetProduct(controller)));
    uint32_t supported = 0;
    for (unsigned i = 0; i < sizeof(ButtonMap) / sizeof(ButtonMap[0]); ++i) {
        if (SDL_GameControllerHasButton(controller, (SDL_GameControllerButton)i)) supported |= ButtonMap[i];
    }
    assert((supported & EdgeButtons) == EdgeButtons);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
    for (int i = 0; i < 4; ++i) {
        SDL_GameControllerButton button = (SDL_GameControllerButton)(SDL_CONTROLLER_BUTTON_PADDLE1 + i);
        for (int down = 1; down >= 0; --down) {
            assert(SDL_JoystickSetVirtualButton(joystick, 20 - i, (Uint8)down) == 0);
            SDL_JoystickUpdate();
            bool found = false;
            while (SDL_PollEvent(&event)) {
                if (event.type == (down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP)) {
                    assert(event.cbutton.button == button);
                    assert(ButtonMap[event.cbutton.button] == (PADDLE1_FLAG << i));
                    found = true;
                }
            }
            assert(found);
            for (int j = 0; j < 4; ++j) {
                assert(SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)(SDL_CONTROLLER_BUTTON_PADDLE1 + j)) == (i == j ? down : 0));
            }
        }
    }
    SDL_GameControllerClose(controller);
    SDL_JoystickClose(joystick);
    assert(SDL_JoystickDetachVirtual(device) == 0);
    SDL_Quit();
#endif
}
