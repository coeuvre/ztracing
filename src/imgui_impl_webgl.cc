#include "src/imgui_impl_webgl.h"

#include <GLES3/gl3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/logging.h"

struct BackendData {
  Allocator allocator;
  GLuint shader_program;
  GLuint vbo, ebo;
  GLuint vao;
  GLuint font_texture;
  GLint attrib_location_pos, attrib_location_uv, attrib_location_color;
  GLint attrib_location_proj_mtx;
  ArrayList<ImDrawVert> vtx_staging;
  ArrayList<ImDrawIdx> idx_staging;
};

static BackendData* get_backend_data() {
  return ImGui::GetCurrentContext()
             ? (BackendData*)ImGui::GetIO().BackendRendererUserData
             : nullptr;
}

static void setup_vertex_attributes(size_t vtx_offset) {
  BackendData* bd = get_backend_data();

  glVertexAttribPointer((GLuint)bd->attrib_location_pos, 2, GL_FLOAT, GL_FALSE,
                        sizeof(ImDrawVert),
                        (GLvoid*)(vtx_offset * sizeof(ImDrawVert) +
                                  offsetof(ImDrawVert, pos)));
  glVertexAttribPointer((GLuint)bd->attrib_location_uv, 2, GL_FLOAT, GL_FALSE,
                        sizeof(ImDrawVert),
                        (GLvoid*)(vtx_offset * sizeof(ImDrawVert) +
                                  offsetof(ImDrawVert, uv)));
  glVertexAttribPointer((GLuint)bd->attrib_location_color, 4, GL_UNSIGNED_BYTE,
                        GL_TRUE, sizeof(ImDrawVert),
                        (GLvoid*)(vtx_offset * sizeof(ImDrawVert) +
                                  offsetof(ImDrawVert, col)));
}

static void setup_render_state(ImDrawData* draw_data, int fb_width,
                               int fb_height) {
  BackendData* bd = get_backend_data();

  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_SCISSOR_TEST);

  glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
  float L = draw_data->DisplayPos.x;
  float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
  float T = draw_data->DisplayPos.y;
  float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
  const float ortho_projection[4][4] = {
      {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {(R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f},
  };
  glUseProgram(bd->shader_program);
  glUniformMatrix4fv(bd->attrib_location_proj_mtx, 1, GL_FALSE,
                     &ortho_projection[0][0]);

  glBindBuffer(GL_ARRAY_BUFFER, bd->vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ebo);

  glEnableVertexAttribArray((GLuint)bd->attrib_location_pos);
  glEnableVertexAttribArray((GLuint)bd->attrib_location_uv);
  glEnableVertexAttribArray((GLuint)bd->attrib_location_color);
}

void imgui_impl_webgl_render_draw_data(ImDrawData* draw_data) {
  int fb_width =
      (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  int fb_height =
      (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0) return;

  BackendData* bd = get_backend_data();
  Allocator allocator = bd->allocator;

  // 1. Calculate total counts and prepare staging buffers
  size_t total_vtx_count = 0;
  size_t total_idx_count = 0;
  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    total_vtx_count += (size_t)cmd_list->VtxBuffer.Size;
    total_idx_count += (size_t)cmd_list->IdxBuffer.Size;
  }

  array_list_resize(&bd->vtx_staging, allocator, total_vtx_count);
  array_list_resize(&bd->idx_staging, allocator, total_idx_count);

  // 2. Concatenate data
  size_t vtx_dst_offset = 0;
  size_t idx_dst_offset = 0;
  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    size_t vtx_count = (size_t)cmd_list->VtxBuffer.Size;
    size_t idx_count = (size_t)cmd_list->IdxBuffer.Size;

    memcpy(bd->vtx_staging.data + vtx_dst_offset, cmd_list->VtxBuffer.Data,
           vtx_count * sizeof(ImDrawVert));
    memcpy(bd->idx_staging.data + idx_dst_offset, cmd_list->IdxBuffer.Data,
           idx_count * sizeof(ImDrawIdx));

    vtx_dst_offset += vtx_count;
    idx_dst_offset += idx_count;
  }

  // 3. One single upload to GPU
  glBindBuffer(GL_ARRAY_BUFFER, bd->vbo);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)(total_vtx_count * sizeof(ImDrawVert)),
               bd->vtx_staging.data, GL_STREAM_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (GLsizeiptr)(total_idx_count * sizeof(ImDrawIdx)),
               bd->idx_staging.data, GL_STREAM_DRAW);

  // 4. Setup state and render
  setup_render_state(draw_data, fb_width, fb_height);

  ImVec2 clip_off = draw_data->DisplayPos;
  ImVec2 clip_scale = draw_data->FramebufferScale;
  size_t global_vtx_offset = 0;
  size_t global_idx_offset = 0;

  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
      const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
      ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                      (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
      ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                      (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

      if (clip_min.x < (float)fb_width && clip_min.y < (float)fb_height &&
          clip_max.x >= 0.0f && clip_max.y >= 0.0f) {
        glScissor((int)clip_min.x, (int)((float)fb_height - clip_max.y),
                  (int)(clip_max.x - clip_min.x),
                  (int)(clip_max.y - clip_min.y));
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID());
        setup_vertex_attributes(global_vtx_offset + pcmd->VtxOffset);
        glDrawElements(
            GL_TRIANGLES, (GLsizei)pcmd->ElemCount,
            sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            (void*)(intptr_t)((global_idx_offset + pcmd->IdxOffset) *
                              sizeof(ImDrawIdx)));
      }
    }
    global_vtx_offset += (size_t)cmd_list->VtxBuffer.Size;
    global_idx_offset += (size_t)cmd_list->IdxBuffer.Size;
  }
}

bool imgui_impl_webgl_init(Allocator allocator) {
  ImGuiIO& io = ImGui::GetIO();
  BackendData* bd =
      (BackendData*)allocator_alloc(allocator, sizeof(BackendData));
  bd->allocator = allocator;
  io.BackendRendererUserData = (void*)bd;
  io.BackendRendererName = "imgui_impl_webgl";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

  const GLchar* vertex_shader =
      "#version 300 es\n"
      "precision highp float;\n"
      "layout (location = 0) in vec2 Pos;\n"
      "layout (location = 1) in vec2 UV;\n"
      "layout (location = 2) in vec4 Color;\n"
      "uniform mat4 ProjMtx;\n"
      "out vec2 Frag_UV;\n"
      "out vec4 Frag_Color;\n"
      "void main()\n"
      "{\n"
      "    Frag_UV = UV;\n"
      "    Frag_Color = Color;\n"
      "    gl_Position = ProjMtx * vec4(Pos.xy, 0, 1);\n"
      "}\n";

  const GLchar* fragment_shader =
      "#version 300 es\n"
      "precision mediump float;\n"
      "in vec2 Frag_UV;\n"
      "in vec4 Frag_Color;\n"
      "uniform sampler2D Texture;\n"
      "layout (location = 0) out vec4 Out_Color;\n"
      "void main()\n"
      "{\n"
      "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
      "}\n";

  GLuint vert_handle = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vert_handle, 1, &vertex_shader, nullptr);
  glCompileShader(vert_handle);
  GLint status;
  glGetShaderiv(vert_handle, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    char buffer[512];
    glGetShaderInfoLog(vert_handle, 512, nullptr, buffer);
    LOG_ERROR("vertex shader compilation failed: %s", buffer);
    return false;
  }

  GLuint frag_handle = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(frag_handle, 1, &fragment_shader, nullptr);
  glCompileShader(frag_handle);
  glGetShaderiv(frag_handle, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    char buffer[512];
    glGetShaderInfoLog(frag_handle, 512, nullptr, buffer);
    LOG_ERROR("fragment shader compilation failed: %s", buffer);
    return false;
  }

  bd->shader_program = glCreateProgram();
  glAttachShader(bd->shader_program, vert_handle);
  glAttachShader(bd->shader_program, frag_handle);
  glLinkProgram(bd->shader_program);
  glGetProgramiv(bd->shader_program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    char buffer[512];
    glGetProgramInfoLog(bd->shader_program, 512, nullptr, buffer);
    LOG_ERROR("shader program linking failed: %s", buffer);
    return false;
  }

  bd->attrib_location_proj_mtx =
      glGetUniformLocation(bd->shader_program, "ProjMtx");
  bd->attrib_location_pos = glGetAttribLocation(bd->shader_program, "Pos");
  bd->attrib_location_uv = glGetAttribLocation(bd->shader_program, "UV");
  bd->attrib_location_color = glGetAttribLocation(bd->shader_program, "Color");

  bd->vtx_staging = {};
  bd->idx_staging = {};

  glGenBuffers(1, &bd->vbo);
  glGenBuffers(1, &bd->ebo);

  glActiveTexture(GL_TEXTURE0);

  if (!imgui_impl_webgl_create_fonts_texture()) return false;

  return true;
}

bool imgui_impl_webgl_create_fonts_texture() {
  ImGuiIO& io = ImGui::GetIO();
  BackendData* bd = get_backend_data();

  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  glGenTextures(1, &bd->font_texture);
  glBindTexture(GL_TEXTURE_2D, bd->font_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  io.Fonts->SetTexID((ImTextureID)(intptr_t)bd->font_texture);

  return true;
}

void imgui_impl_webgl_destroy_fonts_texture() {
  BackendData* bd = get_backend_data();
  if (bd->font_texture) {
    glDeleteTextures(1, &bd->font_texture);
    ImGui::GetIO().Fonts->SetTexID(0);
    bd->font_texture = 0;
  }
}

void imgui_impl_webgl_shutdown() {
  BackendData* bd = get_backend_data();
  glDeleteBuffers(1, &bd->vbo);
  glDeleteBuffers(1, &bd->ebo);
  glDeleteProgram(bd->shader_program);
  imgui_impl_webgl_destroy_fonts_texture();
  Allocator allocator = bd->allocator;
  array_list_deinit(&bd->vtx_staging, allocator);
  array_list_deinit(&bd->idx_staging, allocator);
  allocator_free(allocator, bd, sizeof(BackendData));
  ImGui::GetIO().BackendRendererUserData = nullptr;
}

void imgui_impl_webgl_new_frame() {}
