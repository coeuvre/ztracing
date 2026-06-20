#ifndef ZTRACING_SRC_HEADLESS_GL_H_
#define ZTRACING_SRC_HEADLESS_GL_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HeadlessGLContext {
  void* internal_state;
  unsigned int fbo;
  unsigned int rbo_color;
  int width;
  int height;
} headless_gl_context_t;

// Initializes a headless OpenGL context and binds it to the current thread.
// Also creates an FBO and a Color Renderbuffer of the specified size.
bool headless_gl_init(headless_gl_context_t* ctx, int width, int height);

// Shuts down the headless context and releases all resources.
void headless_gl_shutdown(headless_gl_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_HEADLESS_GL_H_
