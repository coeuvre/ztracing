#ifndef SRC_IMGUI_TYPES_H
#define SRC_IMGUI_TYPES_H

#include <stdint.h>

typedef struct ig_vec2 {
  float x, y;
} ig_vec2_t;

typedef struct ig_vec4 {
  float x, y, z, w;
} ig_vec4_t;

#ifdef __cplusplus
#include "third_party/imgui/imgui.h"
#else
typedef uint32_t ImU32;
#endif

#define IG_COL32(R, G, B, A)                                              \
  (((uint32_t)(A) << 24) | ((uint32_t)(B) << 16) | ((uint32_t)(G) << 8) | \
   ((uint32_t)(R) << 0))

#endif  // SRC_IMGUI_TYPES_H
