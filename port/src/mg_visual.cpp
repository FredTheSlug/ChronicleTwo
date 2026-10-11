#include "common.h"

#include "mg_visual.hpp"

PORT_OVERRIDE u_int *GetScrPad() {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE u_int mgCVisualMDT::CreatePacket(mgCDrawManager *manager) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE u_int mgCVisualFixMDT::CreatePacket(mgCDrawManager *manager) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int mgCVisualMDT::CreateFacePacket(u_int *packet, mgCFace *face) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int mgCVisualMDT::SetMaterialRef(u_long128 *packet, mgMaterial *material, int flags) {
    PS2_UNIMPLEMENTED();
}
