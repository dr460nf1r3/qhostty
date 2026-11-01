// Additions of the Qhostty fork to the libghostty embedding API. They are
// in this header, not in ghostty.h, so that the fork can rebase on upstream
// without conflicts. The definitions are in src/qhostty_c.zig.
#ifndef GHOSTTY_EXT_H
#define GHOSTTY_EXT_H

#include <ghostty.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ghostty_config_global_keybind_cb)(void*,
                                                 ghostty_input_trigger_s,
                                                 const char*,
                                                 uintptr_t);

// Creates a transient systemd scope with the given name and moves the
// process into it. A limit of zero is unset. Returns false on failure.
typedef bool (*ghostty_cgroup_scope_cb)(void*,
                                        const char*,
                                        uint32_t,
                                        uint64_t,
                                        uint64_t);

GHOSTTY_API void ghostty_sync_environ(void);
GHOSTTY_API void ghostty_config_enumerate_global_keybinds(
    ghostty_config_t,
    void*,
    ghostty_config_global_keybind_cb);
GHOSTTY_API void ghostty_app_set_cgroup_scope_cb(ghostty_app_t,
                                                ghostty_cgroup_scope_cb);
GHOSTTY_API void ghostty_surface_free_inherited_config(
    ghostty_surface_t,
    ghostty_surface_config_s*);

#ifdef __linux__
// qhostty_c.OpenGL.FrameType
typedef enum {
  GHOSTTY_FRAME_DMABUF,
  GHOSTTY_FRAME_MEMORY,
} ghostty_frame_e;

// qhostty_c.OpenGL.Frame
//
// A frame of the OpenGL renderer. Rows start at the bottom of the image.
// Pixels are RGBA8 with premultiplied alpha.
typedef struct {
  ghostty_frame_e type;
  uint32_t width;
  uint32_t height;

  // GHOSTTY_FRAME_DMABUF
  uint32_t fourcc;
  uint64_t modifier;
  uint32_t plane_count;
  int fds[4];
  int offsets[4];
  int strides[4];

  // GHOSTTY_FRAME_MEMORY: tightly packed rows of width * 4 bytes.
  const uint8_t* pixels;
  size_t pixels_len;

  void* handle;
} ghostty_frame_s;

GHOSTTY_API bool ghostty_surface_take_frame(ghostty_surface_t,
                                           ghostty_frame_s*);
GHOSTTY_API void ghostty_frame_free(ghostty_frame_s*);
GHOSTTY_API void ghostty_surface_report_presentation_health(
    ghostty_surface_t,
    ghostty_action_renderer_health_e);

// The host must make the inspector context and target framebuffer current
// before calling these functions.
GHOSTTY_API bool ghostty_inspector_opengl_realize(ghostty_inspector_t);
GHOSTTY_API void ghostty_inspector_opengl_render(ghostty_inspector_t);
GHOSTTY_API void ghostty_inspector_opengl_unrealize(ghostty_inspector_t);
#endif

#ifdef __cplusplus
}
#endif

#endif  // GHOSTTY_EXT_H
