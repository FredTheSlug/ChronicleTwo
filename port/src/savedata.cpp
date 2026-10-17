#include "common.h"

#include <cstdlib>
#include <cstring>

#include "bounded_text.hpp"
#include "savedata.hpp"

// Retail copies the 28-byte round name into the 20-byte record name, so a full 20-character name
// wrote its terminator into the next field. The name is cut to the record instead.
PORT_OVERRIDE int CSphidaData::EnterScore() {
    const int total_score = GetHorlScore(-1);

    for (int i = 0; i < SPHIDA_PLAYER_MAX; ++i) {
        SPHIDA_PLAYER_DATA *record = GetPlayerData(i);
        if (record->total_score > total_score) {
            continue;
        }

        for (int j = SPHIDA_PLAYER_MAX - 2; j >= i; --j) {
            SPHIDA_PLAYER_DATA *moved = GetPlayerData(j);
            std::memcpy(moved + 1, moved, sizeof(SPHIDA_PLAYER_DATA));
        }

        bounded_text::Copy(record->name, bounded_text::View(player_name, sizeof player_name));
        record->unk_38 = 1;
        record->total_score = total_score;
        for (int k = 0; k < SPHIDA_HOLE_MAX; ++k) {
            record->hole_score[k] = static_cast<u8>(hole_score[k]);
        }
        record->password_key = std::rand() % 255;
        return i;
    }

    return 0x64;
}
