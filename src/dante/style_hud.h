#pragma once

#include "style.h"

struct ID3D11Device;

// Lifecycle & asset management
void style_hud_init();
void style_hud_load_assets(ID3D11Device* device);
void style_hud_release_assets();

// State notifications
void style_hud_on_switch(StyleType new_style);
void style_hud_update();

// Renders the authentic DMC5 Style Switcher HUD with high-res shattered glass, DMC5 typography, and dynamic animations
void style_hud_render(float screen_w, float screen_h);
