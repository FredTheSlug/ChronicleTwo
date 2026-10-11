#include "common.h"

#include "mglib.hpp"

// Each of these drives DMAC, GS or T0 registers the host does not have, or reads and writes the
// dev-kit's absolute addresses beyond the retail 32 MiB.

PORT_OVERRIDE void mgInit(int screen_mode, int video_mode) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void mgBeginFrame(mgCDrawManager *manager) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void mgEndFrame(mgCDrawManager *manager) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void mgSendPacket(mgCDrawManager *manager) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void StoreImage(int front_buffer) {
    PS2_UNIMPLEMENTED();
}
