#pragma once

#include <cstdint>

#include "vu1/vu1_math.hpp"

// Numbering: Sprite3dQuad is the payload only. Qword indices for the GIF packet follow
// docs/vu1/prog_3dsp.md section 6 (qw0 = RGBAQ, vertex i = UV at qw 1+2i and XYZF2 at qw 2+2i).
// Section 10 counts out[0] as the GIF tag; that offset is not used here.

namespace dc::vu1 {

inline constexpr uint32_t kSprite3dPrimSprite = 0x5E;  // mode 0
inline constexpr uint32_t kSprite3dPrimStrip = 0x5C;   // any other mode

struct Sprite3dParams {
    Mat4 ls;   // data 4-7
    Vec4 vs0;  // data 25, row scaled by the matrix row norm
    Vec4 vs1;  // data 26
    Fog fog;   // data 0x3B
    // Data 0x16 (guard_min) and 0x15 (guard_max). x/y are the screen cull bounds; w is near / far.
    // Written by Vu_progmain from the SCISSOR flag, so the sprite packet does not carry them
    // (docs/vu1/prog_3dsp.md section 9, item 10).
    Vec4 guard_min;
    Vec4 guard_max;
};

struct Sprite3dRecord {
    Vec4 pos;
    Vec4 size;    // {w, h, sin, cos}
    Vec4 colour;  // RGBA as floats, no clamp
    Vec4 uv0;
    Vec4 uv1;
};

struct Sprite3dVertex {
    Vec4 screen;      // x, y, z, F (fog without the ADC bias)
    uint16_t x, y;    // low 16 bits of Ftoi4(x), Ftoi4(y)
    uint32_t z;       // 24-bit Z field
    uint8_t f;        // fog byte, bits 11..4 of the w word
    uint16_t w;       // full w word: bits 11..4 = F, bit 15 = ADC, bits 3..0 = fraction
    bool adc;
    int32_t u, v;     // Ftoi4 of the UV
};

struct Sprite3dQuad {
    int vertex_count;  // 2 (mode 0: min, max) or 4 (strip v0..v3)
    Sprite3dVertex v[4];
    int32_t rgba[4];   // low 8 bits of each colour component
    bool drawn;
};

Sprite3dQuad Sprite3dExpand(const Sprite3dParams& p, const Sprite3dRecord& r, int mode);

// GIF tag NLOOP = count with EOP set.
uint32_t Sprite3dTag(int count);

}  // namespace dc::vu1
