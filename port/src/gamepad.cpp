#include "common.h"

#include "gamepad.hpp"
#include "platform/paths.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
std::vector<PAD_CAPTURE_FRAME> recording;

void Neutral(PAD_STATUS &status) {
    status.button = 0;
    status.left_x = status.left_y = status.right_x = status.right_y = 128;
}
}

PORT_OVERRIDE void CGamePad::CaptureStart() {
    recording.clear();
    capture_frame = 0;
    capture_mode = PAD_CAPTURE_RECORD;
}

PORT_OVERRIDE void CGamePad::CapturePlay() {
    capture_frame = 0;
    capture_mode = PAD_CAPTURE_PLAY;
}

PORT_OVERRIDE void CGamePad::Capture(PAD_STATUS *status) {
    if (!status || capture_frame >= PAD_CAPTURE_FRAME_MAX) return;
    if (capture_frame == 0) recording.clear();
    if (capture_frame != recording.size()) return;
    recording.push_back({static_cast<u16>(status->button), status->left_y, status->left_x,
                          status->right_y, status->right_x});
    ++capture_frame;
}

PORT_OVERRIDE void CGamePad::Play(PAD_STATUS *status) {
    if (!status) return;
    if (capture_frame >= recording.size()) {
        Neutral(*status);
        capture_mode = PAD_CAPTURE_OFF;
        return;
    }
    const auto &frame = recording[capture_frame++];
    status->button = frame.button;
    status->left_y = frame.left_y;
    status->left_x = frame.left_x;
    status->right_y = frame.right_y;
    status->right_x = frame.right_x;
}

PORT_OVERRIDE void CGamePad::SaveCapture() {
    const auto path = PathsSaveRoot() / "key_cap.bin";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    for (const auto &frame : recording) {
        const unsigned char bytes[] = {static_cast<unsigned char>(frame.button),
                                       static_cast<unsigned char>(frame.button >> 8),
                                       frame.left_y, frame.left_x, frame.right_y, frame.right_x};
        file.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
    }
    if (!file) std::fprintf(stderr, "gamepad: could not save key_cap.bin\n");
}

PORT_OVERRIDE void CGamePad::LoadCapture() {
    recording.clear();
    capture_frame = 0;
    capture_mode = PAD_CAPTURE_OFF;
    std::ifstream file(PathsSaveRoot() / "key_cap.bin", std::ios::binary | std::ios::ate);
    if (!file) return;
    const auto size = file.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(PAD_CAPTURE_FRAME_MAX * 6) || size % 6 != 0) {
        std::fprintf(stderr, "gamepad: invalid key_cap.bin size\n");
        return;
    }
    file.seekg(0);
    std::vector<PAD_CAPTURE_FRAME> loaded;
    loaded.reserve(static_cast<std::size_t>(size) / 6);
    for (std::streamoff offset = 0; offset < size; offset += 6) {
        unsigned char bytes[6];
        if (!file.read(reinterpret_cast<char *>(bytes), sizeof(bytes))) return;
        loaded.push_back({static_cast<u16>(bytes[0] | (bytes[1] << 8)), bytes[2], bytes[3], bytes[4], bytes[5]});
    }
    recording = std::move(loaded);
}
