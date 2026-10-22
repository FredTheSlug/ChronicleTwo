#include "common.h"

#include <cstring>

#include "stubs/pbuggy.hpp"

namespace {

struct RetailKey {
    const char *name;  /**< Shift-JIS name of the motion. */
    int         start; /**< First frame of the motion in retail's info.cfg. */
};

// The motions whose frames the train fight compares against numbers, with their first frames in sg/pb/pb.pak:
// mints_bomb.chr's lift (the bomb is taken at 15) and throw (released at 44), and starbre.chr's reload (the bomb
// is in the carrier's hand from 18 and set down at 30.9).
constexpr RetailKey kRetailKeys[] = {
    {"\x8e\x9d\x82\xbf\x8f\xe3\x82\xb0",         11},
    {"\x94\x9a\x92\x65\x93\x8a\x82\xb0",         38},
    {"\x94\x9a\x92\x65\x82\xf0\x8e\xe6\x82\xe9", 9 },
};

} // namespace

// Native motion sets lay their motions out back to back (RegisterActions in platform/model_assets.cpp), so a motion
// keeps retail's length but not its first frame. A frame of one of the motions above is moved to retail's numbering;
// any other frame is kept.
float PortBuggyRetailFrame(CCharacter2 *character) {
    const float frame = character->GetNowFrame();
    const auto *key = character->now_key;
    if (key == nullptr) {
        return frame;
    }
    for (const auto &retail : kRetailKeys) {
        if (std::strcmp(key->name, retail.name) == 0) {
            return frame - static_cast<float>(key->start_frame) + static_cast<float>(retail.start);
        }
    }
    return frame;
}
