#include "coordinates.h"
#include "mod.h"

#include <cstdio>
#include <cstdint>

bool coord_get_player_pos(float& out_x, float& out_y, float& out_z) {
    if (!mod_in_game()) {
        return false;
    }
    uint8_t* player = mod_actor();
    if (!player) {
        return false;
    }
    auto ptr_val = reinterpret_cast<uintptr_t>(player);
    if (ptr_val < 0x10000 || ptr_val > 0x7FFFFFFFFFFFull || (ptr_val & 7) != 0) {
        return false;
    }

    out_x = *reinterpret_cast<float*>(player + 0x1AC0);
    out_y = *reinterpret_cast<float*>(player + 0x1AC4);
    out_z = *reinterpret_cast<float*>(player + 0x1AC8);
    return true;
}

bool coord_format_hud_string(char* out_buf, size_t buf_size) {
    if (!out_buf || buf_size == 0) {
        return false;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!coord_get_player_pos(x, y, z)) {
        out_buf[0] = '\0';
        return false;
    }
    std::snprintf(out_buf, buf_size, "POS // X: %.1f  Y: %.1f  Z: %.1f", x, y, z);
    return true;
}
