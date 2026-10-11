#include "common.h"

#include "mglib.hpp"

#include <eekernel.h>

#include "platform/clock.hpp"

// The retail VSync group's statics (vcount, VSyncCallBack2, rot_priority, call_back_active, old_vcount,
// over_vsync) are read only by functions this port replaces, so the state lives here. VSyncField is the one
// global the rest of the game reads; it is written in place.

namespace {

int (*g_secondary_callback)(int) = nullptr;
int   g_rotate_priority = -1;

} // namespace

PORT_OVERRIDE int VSyncCallBack(int field) {
    VSyncField = !(ClockTickCount() & 1);
    if (g_secondary_callback != nullptr) {
        g_secondary_callback(field);
    }
    return 0;
}

PORT_OVERRIDE void mgInitVSyncCallBack(int (*callback)(int)) {
    g_secondary_callback = callback;
}

PORT_OVERRIDE void mgSetRotateThread(int priority) {
    g_rotate_priority = priority;
}

PORT_OVERRIDE void WaitVSync(int start, int frames) {
    while (ClockTickCount() - start < frames) {
        if (g_rotate_priority > 0) {
            RotateThreadReadyQueue(g_rotate_priority);
        }
        ClockWaitNextTick();
        ClockPump();
    }
}

PORT_OVERRIDE int mgGetVSyncCount() {
    return static_cast<int>(ClockTickCount());
}
