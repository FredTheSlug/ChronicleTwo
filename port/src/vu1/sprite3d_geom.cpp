#include "vu1/sprite3d_geom.hpp"

#include <algorithm>
#include <limits>

namespace dc::vu1 {
namespace {

constexpr float kAdcBias = 2048.0f;

// Open point 14: VU DIV by zero saturates to the largest magnitude (inferred). Any such
// sprite then fails the Q window, so the value only has to stay finite.
float ReciprocalW(float w) {
    return w != 0.0f ? 1.0f / w : std::numeric_limits<float>::max();
}

// RGBAQ: FTOI0 saturates to 16 bits, then the low byte is taken (section 5, no clamp to 0..255).
int32_t ColourByte(float x) {
    int32_t i = Ftoi0(x);
    i = std::clamp(i, -32768, 32767);
    return static_cast<int32_t>(static_cast<uint32_t>(i) & 0xFFu);
}

// Section 5 packing. Z is trunc(16 z) >> 4 (inferred from section 10 case 1).
Sprite3dVertex Pack(Vec4 screen, int32_t u, int32_t v, bool adc) {
    Sprite3dVertex out{};
    out.screen = screen;
    out.x = static_cast<uint16_t>(Ftoi4(screen.x) & 0xFFFF);
    out.y = static_cast<uint16_t>(Ftoi4(screen.y) & 0xFFFF);
    out.z = (static_cast<uint32_t>(Ftoi4(screen.z)) >> 4) & 0xFFFFFFu;
    out.f = static_cast<uint8_t>((static_cast<uint32_t>(Ftoi4(screen.w)) >> 4) & 0xFFu);
    out.w = static_cast<uint16_t>(Ftoi4(adc ? screen.w + kAdcBias : screen.w) & 0xFFFF);
    out.adc = adc;
    out.u = u;
    out.v = v;
    return out;
}

// Section 7: strict inequalities on x, y and the 1/Pw window. NaN fails every test.
// Open point 15: the Q window is the interlocked result, near < Pw < far.
bool Passes(const Sprite3dParams& p, float min_x, float min_y, float max_x, float max_y, float q) {
    return min_x > p.guard_min.x && min_y > p.guard_min.y && max_x < p.guard_max.x &&
           max_y < p.guard_max.y && q > 1.0f / p.guard_max.w && q < 1.0f / p.guard_min.w;
}

}  // namespace

uint32_t Sprite3dTag(int count) {
    // Open point 12: the VU does not guard N = 0; the CPU never emits an empty batch.
    return 0x8000u | static_cast<uint32_t>(count);
}

Sprite3dQuad Sprite3dExpand(const Sprite3dParams& p, const Sprite3dRecord& r, int mode) {
    const Vec4 pclip = Mul(p.ls, r.pos);
    const float q = ReciprocalW(pclip.w);
    const Vec4 c{pclip.x * q, pclip.y * q, pclip.z * q, q};

    const int32_t u0x = Ftoi4(r.uv0.x), u0y = Ftoi4(r.uv0.y);
    const int32_t u1x = Ftoi4(r.uv1.x), u1y = Ftoi4(r.uv1.y);

    const float fog = FogValue(p.fog, q);

    Sprite3dQuad out{};
    out.rgba[0] = ColourByte(r.colour.x);
    out.rgba[1] = ColourByte(r.colour.y);
    out.rgba[2] = ColourByte(r.colour.z);
    out.rgba[3] = ColourByte(r.colour.w);


    if (mode == 0) {
        const float hqx = (r.size.x * p.vs0.x) * 0.5f * q;
        const float hqy = (r.size.y * p.vs1.y) * 0.5f * q;
        const Vec4 min{c.x - hqx, c.y - hqy, c.z, fog};
        const Vec4 max{c.x + hqx, c.y + hqy, c.z, fog};

        out.drawn = Passes(p, min.x, min.y, max.x, max.y, q);
        // Open point 2: rejected mode-0 vertices are ADC-tagged and assumed not drawn by the GS.
        const bool adc = !out.drawn;

        out.vertex_count = 2;
        out.v[0] = Pack(min, u0x, u0y, adc);
        out.v[1] = Pack(max, u1x, u1y, adc);
        return out;
    }

    // Open point 3: the VU takes the rotated path for any nonzero mode, so modes >= 2 are strips.
    const Vec4 a = Vec4{r.size.w, r.size.z, 0.0f, 0.0f} * p.vs0.x * (r.size.x * 0.5f) * q;
    const Vec4 b = Vec4{-r.size.z, r.size.w, 0.0f, 0.0f} * p.vs1.y * (r.size.y * 0.5f) * q;
    const Vec4 sum = a + b;
    const Vec4 diff = b - a;

    const Vec4 corner[4] = {
        {c.x - sum.x, c.y - sum.y, c.z, fog},
        {c.x - diff.x, c.y - diff.y, c.z, fog},
        {c.x + diff.x, c.y + diff.y, c.z, fog},
        {c.x + sum.x, c.y + sum.y, c.z, fog},
    };
    const float min_x = std::min({corner[0].x, corner[1].x, corner[2].x, corner[3].x});
    const float max_x = std::max({corner[0].x, corner[1].x, corner[2].x, corner[3].x});
    const float min_y = std::min({corner[0].y, corner[1].y, corner[2].y, corner[3].y});
    const float max_y = std::max({corner[0].y, corner[1].y, corner[2].y, corner[3].y});

    out.drawn = Passes(p, min_x, min_y, max_x, max_y, q);

    // Section 7: v0 and v1 are always ADC-tagged; v2 and v3 only when the sprite is rejected.
    const bool reject = !out.drawn;
    const int32_t uvs[4][2] = {{u0x, u0y}, {u1x, u0y}, {u0x, u1y}, {u1x, u1y}};
    const bool adc[4] = {true, true, reject, reject};

    out.vertex_count = 4;
    for (int i = 0; i < 4; ++i) {
        out.v[i] = Pack(corner[i], uvs[i][0], uvs[i][1], adc[i]);
    }
    return out;
}

}  // namespace dc::vu1
