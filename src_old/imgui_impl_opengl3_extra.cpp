// A copy of ImGui_ImplOpenGL3_RenderDrawData that doesn't save & restore OpenGL
// state becaues it hurts performance on software webgl.
static void ImGui_ImplOpenGL3_RenderDrawDataNoStateSaving(
    ImDrawData *draw_data) {
  // Avoid rendering when minimized, scale coordinates for retina displays
  // (screen coordinates != framebuffer coordinates)
  int fb_width =
      (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  int fb_height =
      (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0) return;

  ImGui_ImplOpenGL3_Data *bd = ImGui_ImplOpenGL3_GetBackendData();

  glActiveTexture(GL_TEXTURE0);

  // Setup desired GL state
  // Recreate the VAO every time (this is to easily allow multiple GL contexts
  // to be rendered to. VAO are not shared among GL contexts) The renderer
  // would actually work without any VAO bound, but then our VertexAttrib
  // calls would overwrite the default one currently bound.
  GLuint vertex_array_object = 0;
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
  GL_CALL(glGenVertexArrays(1, &vertex_array_object));
#endif
  ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height,
                                     vertex_array_object);

  // Will project scissor/clipping rectangles into framebuffer space
  ImVec2 clip_off =
      draw_data->DisplayPos;  // (0,0) unless using multi-viewports
  ImVec2 clip_scale =
      draw_data->FramebufferScale;  // (1,1) unless using retina display which
                                    // are often (2,2)

  // Render command lists
  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList *cmd_list = draw_data->CmdLists[n];

    // Upload vertex/index buffers
    // - OpenGL drivers are in a very sorry state nowadays....
    //   During 2021 we attempted to switch from glBufferData() to
    //   orphaning+glBufferSubData() following reports of leaks on Intel GPU
    //   when using multi-viewports on Windows.
    // - After this we kept hearing of various display corruptions issues.
    // We started disabling on non-Intel GPU, but issues still got reported
    // on Intel.
    // - We are now back to using exclusively glBufferData(). So
    // bd->UseBufferSubData IS ALWAYS FALSE in this code.
    //   We are keeping the old code path for a while in case people finding
    //   new issues may want to test the bd->UseBufferSubData path.
    // - See https://github.com/ocornut/imgui/issues/4468 and please report
    // any corruption issues.
    const GLsizeiptr vtx_buffer_size =
        (GLsizeiptr)cmd_list->VtxBuffer.Size * (int)sizeof(ImDrawVert);
    const GLsizeiptr idx_buffer_size =
        (GLsizeiptr)cmd_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx);
    if (bd->UseBufferSubData) {
      if (bd->VertexBufferSize < vtx_buffer_size) {
        bd->VertexBufferSize = vtx_buffer_size;
        GL_CALL(glBufferData(GL_ARRAY_BUFFER, bd->VertexBufferSize, nullptr,
                             GL_STREAM_DRAW));
      }
      if (bd->IndexBufferSize < idx_buffer_size) {
        bd->IndexBufferSize = idx_buffer_size;
        GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, bd->IndexBufferSize,
                             nullptr, GL_STREAM_DRAW));
      }
      GL_CALL(glBufferSubData(GL_ARRAY_BUFFER, 0, vtx_buffer_size,
                              (const GLvoid *)cmd_list->VtxBuffer.Data));
      GL_CALL(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_buffer_size,
                              (const GLvoid *)cmd_list->IdxBuffer.Data));
    } else {
      GL_CALL(glBufferData(GL_ARRAY_BUFFER, vtx_buffer_size,
                           (const GLvoid *)cmd_list->VtxBuffer.Data,
                           GL_STREAM_DRAW));
      GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size,
                           (const GLvoid *)cmd_list->IdxBuffer.Data,
                           GL_STREAM_DRAW));
    }

    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
      const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
      if (pcmd->UserCallback != nullptr) {
        // User callback, registered via ImDrawList::AddCallback()
        // (ImDrawCallback_ResetRenderState is a special callback value
        // used by the user to request the renderer to reset render
        // state.)
        if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
          ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height,
                                             vertex_array_object);
        else
          pcmd->UserCallback(cmd_list, pcmd);
      } else {
        // Project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                        (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
        ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                        (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
        if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;

        // Apply scissor/clipping rectangle (Y is inverted in OpenGL)
        GL_CALL(glScissor((int)clip_min.x, (int)((float)fb_height - clip_max.y),
                          (int)(clip_max.x - clip_min.x),
                          (int)(clip_max.y - clip_min.y)));

        // Bind texture, Draw
        GL_CALL(
            glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID()));
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
        if (bd->GlVersion >= 320)
          GL_CALL(glDrawElementsBaseVertex(
              GL_TRIANGLES, (GLsizei)pcmd->ElemCount,
              sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
              (void *)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)),
              (GLint)pcmd->VtxOffset));
        else
#endif
          GL_CALL(glDrawElements(
              GL_TRIANGLES, (GLsizei)pcmd->ElemCount,
              sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
              (void *)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx))));
      }
    }
  }

  // Destroy the temporary VAO
#ifdef IMGUI_IMPL_OPENGL_USE_VERTEX_ARRAY
  GL_CALL(glDeleteVertexArrays(1, &vertex_array_object));
#endif

  (void)bd;  // Not all compilation paths use this
}
