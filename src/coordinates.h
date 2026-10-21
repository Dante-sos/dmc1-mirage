#pragma once

#include <cstddef>

// Retrieves current player position in 3D world space.
// Returns true if player actor is valid and in-game, false otherwise.
bool coord_get_player_pos(float& out_x, float& out_y, float& out_z);

// Formats live coordinates into a compact string for HUD display.
// e.g., "POS // X: 12.3  Y: 10.0  Z: -45.6"
// Returns true if coordinates were formatted, false if player actor not active.
bool coord_format_hud_string(char* out_buf, size_t buf_size);
