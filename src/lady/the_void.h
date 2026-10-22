#pragma once

#include <cstdint>
#include <windows.h>

struct XInputState;

void void_init();
void void_arm(int mode);
void void_disarm();
bool void_is_active();

uint32_t void_get_stage();
uint32_t void_get_room();
void void_get_spawn_pos(float out_pos[4]);

void void_patch_mission_table(bool enable);
void void_patch_briefing_skip(bool enable);
void void_patch_spawn_check(bool enable);
void void_patch_wave_descriptors(bool enable);
void void_patch_style_placement(bool enable);
void void_reset_style_rank();
void void_on_menu_loop(uint8_t* config);
void void_on_menu_launch(uint8_t* config);
void void_update_frame();
void void_on_pad(XInputState* state);
