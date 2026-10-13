#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>

#include "mg_drawprim.hpp"
#include "mg_visual_port.hpp"
#include "visualmotion.hpp"

namespace {

struct alignas(16) MotionModel {
    MDT_HEADER    header{};
    sceVu0FVECTOR positions[3] = {
        {0, 0, 0, 1},
        {1, 0, 0, 1},
        {0, 1, 0, 1}
    };
    MDT_FACES faces{};
    u_int     primitive[6] = {static_cast<u_int>(MG_PRIM_TRIANGLE) | MG_FACE_NO_NORMAL | MG_FACE_NO_TEXTURE, 3, 0, 0, 1, 2};

    MotionModel() {
        header.header_size = sizeof(header);
        header.vertex_num = 3;
        header.vertex_ofs = offsetof(MotionModel, positions);
        header.faces_ofs = offsetof(MotionModel, faces);
        faces.prim_num = 1;
    }
};

struct alignas(16) WeightBlock {
    u_int frame;
    int   bone;
    u_int reserved[2]{};
    u_int count = 3;
    u_int next;
    u_int padding[2]{};

    struct Entry {
        u_int vertex;
        u_int reserved[3]{};
        float percent;
        u_int padding[3]{};
    } entries[3];
};

struct AssignmentFixture {
    MotionModel                 model;
    std::array<u_long128, 1024> storage{};
    std::array<u_long128, 1024> scratch{};
    mgCMemory                   memory;
    mgCMemory                   work;
    WeightBlock                 weights[3] = {
        {1, 4, {}, 3, sizeof(WeightBlock), {}, {{0, {}, 25}, {1, {}, 25}, {2, {}, 25}}   },
        {1, 7, {}, 3, sizeof(WeightBlock), {}, {{0, {}, 75}, {1, {}, 75}, {2, {}, 75}}   },
        {2, 9, {}, 3, 0,                   {}, {{0, {}, 100}, {1, {}, 100}, {2, {}, 100}}},
    };

    AssignmentFixture() {
        memory.stSetBuffer(storage.data(), storage.size());
        work.stSetBuffer(scratch.data(), scratch.size());
    }

    int Assign(mgCVisualMotionMDT &visual, int frame) {
        mgCVMotionData motion{};
        motion.weight_data = reinterpret_cast<u_int *>(weights);
        motion.frame_id = frame;
        return visual.DataAssignMotionMDT(&model.header, &motion, &memory, &work, nullptr);
    }
};

gfx::Prog0Skin SkinAt(const mgVisualFaceMesh &mesh, int vertex) {
    gfx::Prog0Skin skin{};
    std::memcpy(&skin, &mesh.vertices[2 * vertex + 1], sizeof(skin));
    return skin;
}

} // namespace

TEST(VisualMotion, SharedWorkStackPreservesAssignedWeights) {
    AssignmentFixture  fixture;
    mgCVisualMotionMDT first;
    mgCVisualMotionMDT second;
    ASSERT_EQ(fixture.Assign(first, 1), 1);
    std::array<mgVertexWeight, 3> saved;
    std::memcpy(saved.data(), first.weight, sizeof(saved));
    ASSERT_EQ(fixture.Assign(second, 2), 1);

    const auto *assigned_mesh = mgVisualPortFaceMesh(first.face_group->face);
    ASSERT_NE(assigned_mesh, nullptr);
    ASSERT_EQ(assigned_mesh->vertices.size(), 6u);
    EXPECT_FLOAT_EQ(SkinAt(*assigned_mesh, 0).weight[0], 0.25f);
    EXPECT_FLOAT_EQ(SkinAt(*assigned_mesh, 0).weight[1], 0.75f);
    EXPECT_NE(first.weight, second.weight);
    EXPECT_EQ(std::memcmp(first.weight, saved.data(), sizeof(saved)), 0);
    fixture.work.stReset();
    std::memset(fixture.work.stack, 0, fixture.work.stack_size * 16);

    int     indices[3] = {0, 1, 2};
    mgCFace face{};
    face.type = fixture.model.primitive[0];
    face.index_stride = 1;
    face.index_num = face.vertex_num = 3;
    face.index = indices;
    const auto first_mesh = mgVisualPortBuildFace(first, face, true);
    const auto second_mesh = mgVisualPortBuildFace(second, face, true);
    ASSERT_EQ(first_mesh.vertices.size(), 6u);
    ASSERT_EQ(second_mesh.vertices.size(), 6u);
    for (int vertex = 0; vertex < 3; ++vertex) {
        const auto first_skin = SkinAt(first_mesh, vertex);
        const auto second_skin = SkinAt(second_mesh, vertex);
        EXPECT_FLOAT_EQ(first_skin.weight[0], 0.25f);
        EXPECT_FLOAT_EQ(first_skin.weight[1], 0.75f);
        EXPECT_EQ(first_skin.bone[1], 1);
        EXPECT_FLOAT_EQ(second_skin.weight[0], 1.0f);
        EXPECT_FLOAT_EQ(second_skin.weight[1], 0.0f);
    }
}
