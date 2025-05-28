#include "flick_web.h"

#include <emscripten/emscripten.h>
#include <stdint.h>

#include "flick.h"

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseButtonDown(FL_f32 x, FL_f32 y, FL_u32 button) {
  FL_OnMouseButtonDown((FL_Vec2){x, y}, button);
}

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseButtonUp(FL_f32 x, FL_f32 y, FL_u32 button) {
  FL_OnMouseButtonUp((FL_Vec2){x, y}, button);
}

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseMove(FL_f32 x, FL_f32 y) { FL_OnMouseMove((FL_Vec2){x, y}); }

EMSCRIPTEN_KEEPALIVE
void FLJS_OnMouseScroll(FL_f32 pos_x, FL_f32 pos_y, FL_f32 delta_x,
                        FL_f32 delta_y) {
  FL_OnMouseScroll((FL_Vec2){pos_x, pos_y}, (FL_Vec2){delta_x, delta_y});
}

extern FL_isize FLJS_Renderer_CreateTexture(FL_i32 width, FL_i32 height);
extern FL_isize FLJS_Renderer_UpdateTexture(FL_isize texture, FL_i32 x,
                                            FL_i32 y, FL_i32 width,
                                            FL_i32 height, void *pixels);
extern void FLJS_Renderer_SetBufferData(void *vtx_buffer_ptr,
                                        FL_isize vtx_buffer_len,
                                        void *idx_buffer_ptr,
                                        FL_isize idx_buffer_len);

extern void FLJS_Renderer_Draw(FL_f32 left, FL_f32 top, FL_f32 right,
                               FL_f32 bottom, FL_isize texture,
                               FL_i32 idx_count, FL_i32 idx_offset);

extern void FLJS_CheckCanvasSize(FL_Vec2 *size, FL_f32 *pixels_per_point);

FL_Vec2 FLJS_BeginFrame(void) {
  FL_Vec2 canvas_size;
  FL_f32 pixels_per_point;
  FLJS_CheckCanvasSize(&canvas_size, &pixels_per_point);
  FL_SetPixelsPerPoint(pixels_per_point);
  return canvas_size;
}

static void RunTextureCommand(FL_TextureCommand *command) {
  if (command->texture->id) {
    FLJS_Renderer_UpdateTexture((FL_isize)command->texture->id, command->x,
                                command->y, command->width, command->height,
                                command->pixels);
  } else {
    command->texture->id =
        (void *)FLJS_Renderer_CreateTexture(command->width, command->height);
  }
}

static void RunDrawCommand(FL_DrawCommand *command) {
  FLJS_Renderer_Draw(command->clip_rect.left, command->clip_rect.top,
                     command->clip_rect.right, command->clip_rect.bottom,
                     (FL_isize)command->texture->id, command->index_count,
                     command->index_offset);
}

#if 0
static void DebugDrawTexture(FL_isize texture_id) {
  FL_DrawVertex vtx[] = {
      {.pos = {0, 0}, .uv = {0, 0}, .color = FL_COLOR_RGB(255, 255, 255)},
      {.pos = {819.2f, 0}, .uv = {1, 0}, .color = FL_COLOR_RGB(255, 255, 255)},
      {.pos = {819.2f, 819.2f}, .uv = {1, 1}, .color = FL_COLOR_RGB(255, 255, 255)},
      {.pos = {0, 819.2f}, .uv = {0, 1}, .color = FL_COLOR_RGB(255, 255, 255)},
  };
  FL_u32 idx[] = {0, 1, 2, 0, 2, 3};
  FLJS_Renderer_SetBufferData(vtx, sizeof(vtx), idx, sizeof(idx));
  FLJS_Renderer_Draw(0, 0, 10000, 10000, texture_id, 6, 0);
}
#endif

void FLJS_EndFrame(FL_DrawList *draw_list) {
  for (FL_isize i = 0; i < draw_list->texture_command_count; ++i) {
    RunTextureCommand(draw_list->texture_commands + i);
  }

  FLJS_Renderer_SetBufferData(
      draw_list->vertex_buffer, draw_list->vertex_count * sizeof(FL_DrawVertex),
      draw_list->index_buffer, draw_list->index_count * sizeof(FL_DrawIndex));

  for (FL_isize i = 0; i < draw_list->draw_command_count; ++i) {
    RunDrawCommand(draw_list->draw_commands + i);
  }
}
