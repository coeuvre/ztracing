#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/logging.h"
#include "src/headless_gl.h"

typedef struct HeadlessGLInternal {
  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
} headless_gl_internal_t;

bool headless_gl_init(headless_gl_context_t* ctx, int width, int height) {
  // Force EGL to use surfaceless platform if not already specified,
  // which prevents it from trying to connect to X11/Wayland.
  setenv("EGL_PLATFORM", "surfaceless", 0);

  ctx->width = width;
  ctx->height = height;

  headless_gl_internal_t* internal =
      (headless_gl_internal_t*)malloc(sizeof(headless_gl_internal_t));
  if (!internal) {
    LOG_ERROR("Failed to allocate memory for headless GL internal state");
    return false;
  }
  ctx->internal_state = internal;

  // 1. Get EGL Display
  internal->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (internal->display == EGL_NO_DISPLAY) {
    LOG_ERROR("Failed to get EGL display (error: 0x%x)", (int)eglGetError());
    free(internal);
    return false;
  }

  // 2. Initialize EGL
  EGLint major, minor;
  if (!eglInitialize(internal->display, &major, &minor)) {
    LOG_ERROR("Failed to initialize EGL (error: 0x%x)", (int)eglGetError());
    free(internal);
    return false;
  }
  LOG_INFO("Initialized EGL %d.%d", (int)major, (int)minor);

  // 3. Choose EGL Config
  EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                             EGL_PBUFFER_BIT,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_RED_SIZE,
                             8,
                             EGL_ALPHA_SIZE,
                             8,
                             EGL_DEPTH_SIZE,
                             0,
                             EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_ES3_BIT,
                             EGL_NONE};

  EGLConfig config;
  EGLint num_configs;
  if (!eglChooseConfig(internal->display, config_attribs, &config, 1,
                       &num_configs) ||
      num_configs == 0) {
    LOG_ERROR("Failed to choose EGL config (error: 0x%x)", (int)eglGetError());
    eglTerminate(internal->display);
    free(internal);
    return false;
  }

  // 4. Create EGL Context
  EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

  internal->context = eglCreateContext(internal->display, config,
                                       EGL_NO_CONTEXT, context_attribs);
  if (internal->context == EGL_NO_CONTEXT) {
    LOG_ERROR("Failed to create EGL context (error: 0x%x)", (int)eglGetError());
    eglTerminate(internal->display);
    free(internal);
    return false;
  }

  // 5. Create Pbuffer Surface
  EGLint pbuffer_attribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};

  internal->surface =
      eglCreatePbufferSurface(internal->display, config, pbuffer_attribs);
  if (internal->surface == EGL_NO_SURFACE) {
    LOG_ERROR("Failed to create EGL pbuffer surface (error: 0x%x)",
              (int)eglGetError());
    eglDestroyContext(internal->display, internal->context);
    eglTerminate(internal->display);
    free(internal);
    return false;
  }

  // 6. Make Current
  if (!eglMakeCurrent(internal->display, internal->surface, internal->surface,
                      internal->context)) {
    LOG_ERROR("Failed to make EGL context current (error: 0x%x)",
              (int)eglGetError());
    eglDestroySurface(internal->display, internal->surface);
    eglDestroyContext(internal->display, internal->context);
    eglTerminate(internal->display);
    free(internal);
    return false;
  }

  // 7. Create FBO
  glGenFramebuffers(1, &ctx->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);

  glGenRenderbuffers(1, &ctx->rbo_color);
  glBindRenderbuffer(GL_RENDERBUFFER, ctx->rbo_color);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, ctx->rbo_color);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    LOG_ERROR("Framebuffer is not complete (status: 0x%x)", (int)status);
    glDeleteFramebuffers(1, &ctx->fbo);
    glDeleteRenderbuffers(1, &ctx->rbo_color);
    eglMakeCurrent(internal->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    eglDestroySurface(internal->display, internal->surface);
    eglDestroyContext(internal->display, internal->context);
    eglTerminate(internal->display);
    free(internal);
    return false;
  }

  LOG_INFO("Headless GL context initialized successfully (FBO: %u)", ctx->fbo);
  return true;
}

void headless_gl_shutdown(headless_gl_context_t* ctx) {
  if (!ctx || !ctx->internal_state) return;
  headless_gl_internal_t* internal =
      (headless_gl_internal_t*)ctx->internal_state;

  if (ctx->fbo) {
    glDeleteFramebuffers(1, &ctx->fbo);
    ctx->fbo = 0;
  }
  if (ctx->rbo_color) {
    glDeleteRenderbuffers(1, &ctx->rbo_color);
    ctx->rbo_color = 0;
  }

  if (internal->display != EGL_NO_DISPLAY) {
    eglMakeCurrent(internal->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (internal->surface != EGL_NO_SURFACE) {
      eglDestroySurface(internal->display, internal->surface);
    }
    if (internal->context != EGL_NO_CONTEXT) {
      eglDestroyContext(internal->display, internal->context);
    }
    eglTerminate(internal->display);
  }

  free(internal);
  ctx->internal_state = nullptr;
  LOG_INFO("Headless GL context shut down.");
}
