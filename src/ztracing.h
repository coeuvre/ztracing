#pragma once

#include "core.h"

#define IM_ASSERT(x) ASSERT(x, "")
#include <imgui.h>

struct ZTracing {
    bool show_demo_window;
};

void ztracing_update(ZTracing *ztracing);
