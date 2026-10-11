#include "common.h"

#include "movie.hpp"

// Each of these programs DMAC/IPU/VIF registers, or converts to the PS2's uncached or physical
// mirrors of RAM, or reads the MIPS $gp that the host toolchain does not provide.

PORT_OVERRIDE void CMovie::Load(char *name, mgCMemory **memory, int width, int height, bool with_audio, bool loop,
                                bool init_sound) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void CMovie::Play(char *path) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int CMovie::videoDecFlush(VideoDec *dec) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int videoCallback(sceMpeg *mpeg, sceMpegCbDataStr *str, void *user) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void setD3_CHCR(u32 chcr) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void setD4_CHCR(u32 chcr) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int viBufCreate(ViBuf *buf, u_long128 *data, u_long128 *tags, int sectors, TimeStamp *ts, int ts_count) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int viBufReset(ViBuf *buf) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int viBufAddDMA(ViBuf *buf) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int viBufStopDMA(ViBuf *buf) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int viBufRestartDMA(ViBuf *buf) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int viBufDelete(ViBuf *buf) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE int viBufGetTs(ViBuf *buf, TimeStamp *ts) {
    PS2_UNIMPLEMENTED();
}

PORT_OVERRIDE void setImageTag(u32 *tag, void *data, int a, int width, int height) {
    PS2_UNIMPLEMENTED();
}
