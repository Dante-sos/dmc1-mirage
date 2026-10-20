#pragma once

#include <cstdint>

struct XInputState;

void trickster_init();
void trickster_request_action(const XInputState* state);
void trickster_on_action(XInputState* state);
void trickster_update();
bool trickster_is_dashing();
bool trickster_is_invulnerable();
const char* trickster_action_name();
