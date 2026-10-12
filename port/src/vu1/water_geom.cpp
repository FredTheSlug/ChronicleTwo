#include "vu1/water_geom.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace dc::vu1 {

namespace {

// Retail's stencil writes out of bounds for tiny grids; out-of-range taps are dropped here.
void ApplyEdgeWeights(std::vector<float>& weight, int rows, int columns) {
    const auto set = [&](int r, int c, float value) {
        if (r >= 0 && r < rows && c >= 0 && c < columns) {
            weight[static_cast<std::size_t>(r * columns + c)] = value;
        }
    };

    for (int r = 0; r < rows; ++r) {
        set(r, columns - 1, 0.0f);
        set(r, 0, 0.0f);
        set(r, columns - 2, 0.6f);
        set(r, 1, 0.6f);
        set(r, columns - 3, 0.3f);
        set(r, 2, 0.3f);
    }
    // Row edges overwrite the column stencil for the whole row.
    for (int c = 0; c < columns; ++c) {
        set(rows - 1, c, 0.0f);
        set(0, c, 0.0f);
        set(rows - 2, c, 0.6f);
        set(1, c, 0.6f);
        set(rows - 3, c, 0.3f);
        set(2, c, 0.3f);
    }
}

}  // namespace

WaterMesh BuildWaterMesh(const WaterMeshDesc& desc, std::span<const float> height) {
    const int rows = desc.rows;
    const int columns = desc.columns;
    const int count = rows * columns;

    std::vector<float> weight(static_cast<std::size_t>(count), 1.0f);
    ApplyEdgeWeights(weight, rows, columns);

    const float dx = (desc.max.x - desc.min.x) / static_cast<float>(rows - 1);
    const float dz = (desc.max.z - desc.min.z) / static_cast<float>(columns - 1);

    WaterMesh mesh;
    mesh.reserve(static_cast<std::size_t>(rows - 1));
    for (int row = 0; row + 1 < rows; ++row) {
        std::vector<WaterStripVertex> strip;
        strip.reserve(static_cast<std::size_t>(2 * columns));

        // Retail steps columns by 27 but pointers by 26 (open point 9.7), so it drops columns for N >= 28.
        // The PC emits every column.
        for (int c = 0; c < columns; ++c) {
            for (int r = 0; r < 2; ++r) {
                const int i = row + r;
                const std::size_t cell = static_cast<std::size_t>(i * columns + c);

                float slope_x = 0.0f;
                if (i > 0) {
                    slope_x = height[cell - static_cast<std::size_t>(columns)] - height[cell];
                }

                // Open point 9.6: at the last column the retail read is h[i][c+1] past the row, which is
                // h[i+1][0], and past the array for the last row. The PC uses 0 for the past-the-array case.
                float slope_y = 0.0f;
                const std::size_t next = cell + 1;
                if (next < static_cast<std::size_t>(count)) {
                    slope_y = height[cell] - height[next];
                }

                WaterStripVertex v{};
                v.pos = {desc.min.x + static_cast<float>(i) * dx,
                         desc.min.y,
                         desc.min.z + static_cast<float>(c) * dz,
                         1.0f};
                // slope[2] is stale on retail; no output word depends on it (section 5.3).
                v.slope = {slope_x, slope_y, 0.0f, weight[cell]};
                strip.push_back(v);
            }
        }
        mesh.push_back(std::move(strip));
    }
    return mesh;
}

WaterVertex WaterTransform(const WaterParams& params, Vec4 pos, Vec4 slope,
                           uint32_t prev_pass0, uint32_t prev_pass1) {
    constexpr float kFadeA = 375.0f;
    constexpr float kFadeB = -0.25f;
    constexpr float kAdcBias = 2048.0f;

    const Vec4 clip = Mul(params.r, Vec4{pos.x, pos.y * params.p0, pos.z, 1.0f});
    const float q = 1.0f / clip.w;
    const float xw = clip.x * q;
    const float yw = clip.y * q;
    const float zw = clip.z * q;

    const float s3a = params.colour.w * slope.w;
    const float fade0 = kFadeA * q + kFadeB;
    const float fade = std::max(std::min(fade0, 1.0f), 0.0f);
    const float alpha = s3a * fade;

    // Near (guard_min.w) is not tested: primary reading of open point 9.4.
    const float far_w = params.guard_max.w;
    const uint32_t pass_x = (params.guard_min.x < xw && xw < params.guard_max.x) ? 0x80u : 0u;
    const uint32_t pass_y = (params.guard_min.y < yw && yw < params.guard_max.y) ? 0x40u : 0u;
    const uint32_t pass_w = (1.0f / far_w < q && q < far_w) ? 0x10u : 0u;
    const uint32_t pass = pass_x | pass_y | pass_w;

    const uint32_t window = prev_pass0 & prev_pass1 & pass;
    const bool adc = window != kGuardMask;

    float fog = FogValue(params.fog, q);
    if (adc) {
        fog += kAdcBias;
    }

    float ux = (xw - params.ox) + slope.x * params.p1;
    float uy = (yw - params.oy) + slope.y * params.p1;
    ux = std::min(std::max(ux, 1.0f), params.width);
    uy = std::min(std::max(uy, 1.0f), params.height - 1.0f);

    // Open point 9.5: Z is bits 27..4 of FTOI4(zw), 24 bits wide.
    // Open point 9.2: X/Y keep the low 16 bits of a 32-bit FTOI4; ADC is bit 15 of the fog word.
    const uint32_t fog_word = static_cast<uint32_t>(Ftoi4(fog));

    WaterVertex out{};
    out.r = Ftoi0(params.colour.x);
    out.g = Ftoi0(params.colour.y);
    out.b = Ftoi0(params.colour.z);
    out.a = Ftoi0(alpha);
    out.u = Ftoi4(ux);
    out.v = Ftoi4(uy);
    out.x = static_cast<uint16_t>(Ftoi4(xw));
    out.y = static_cast<uint16_t>(Ftoi4(yw));
    out.z = (static_cast<uint32_t>(Ftoi4(zw)) >> 4) & 0xFFFFFFu;
    out.f = static_cast<uint8_t>((fog_word >> 4) & 0xFFu);
    out.adc = ((fog_word >> 15) & 1u) != 0;
    out.pass_bits = pass;
    return out;
}

std::vector<WaterVertex> WaterStripTransform(const WaterParams& params,
                                             std::span<const WaterStripVertex> strip,
                                             uint32_t carry0, uint32_t carry1) {
    std::vector<WaterVertex> out;
    out.reserve(strip.size());

    uint32_t prev0 = carry0;
    uint32_t prev1 = carry1;
    for (const WaterStripVertex& v : strip) {
        const WaterVertex transformed = WaterTransform(params, v.pos, v.slope, prev0, prev1);
        out.push_back(transformed);
        prev0 = prev1;
        prev1 = transformed.pass_bits;
    }
    return out;
}

std::vector<bool> WaterKicks(std::span<const WaterVertex> strip) {
    std::vector<bool> kicked(strip.size(), false);
    for (std::size_t j = 2; j < strip.size(); ++j) {
        const uint32_t window = strip[j - 2].pass_bits & strip[j - 1].pass_bits & strip[j].pass_bits;
        kicked[j] = window == kGuardMask;
    }
    return kicked;
}

WaterRenderState WaterState(bool fog) {
    constexpr uint32_t kPrimMode = 0x158u;
    constexpr uint32_t kPrimFog = 0x20u;
    constexpr uint32_t kPsmct24 = 0x01u;
    constexpr uint32_t kTexaAlpha = 0x80u;

    return WaterRenderState{
        .prmode = kPrimMode | (fog ? kPrimFog : 0u),
        .psm = kPsmct24,
        .ta0 = kTexaAlpha,
        .ta1 = kTexaAlpha,
        .aem = false,
    };
}

}  // namespace dc::vu1
