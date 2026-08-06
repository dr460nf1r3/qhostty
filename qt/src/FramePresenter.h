#pragma once

#include <QSize>

#include <ghostty.h>

class QOpenGLContext;

// Shows the frames of the Ghostty renderer in a Qt OpenGL context.
//
// Ghostty renders on its own thread and exports each frame as a DMABUF or
// in CPU memory. This class imports the frames and copies them into the
// framebuffer of the Qt context. Call all functions with the Qt context
// current.
class FramePresenter final {
 public:
  FramePresenter() = default;
  ~FramePresenter() = default;
  FramePresenter(const FramePresenter&) = delete;
  FramePresenter& operator=(const FramePresenter&) = delete;

  // Returns the EGL display of the context, or nullptr if the context
  // does not use EGL.
  [[nodiscard]] static void* eglDisplay(QOpenGLContext* context);

  // Returns true if the context can import DMABUF frames.
  bool initialize(QOpenGLContext* context);
  void release();

  // Takes ownership of the frame. Returns false if the frame is a DMABUF
  // that the context cannot import.
  bool update(ghostty_frame_s& frame);

  // Copies the latest frame to the top-left corner of the framebuffer.
  void paint(unsigned int framebuffer, QSize size);

 private:
  bool importDmabuf(const ghostty_frame_s& frame);
  void uploadMemory(const ghostty_frame_s& frame);
  void attachTexture(QSize size);
  void destroyImage();

  QOpenGLContext* m_context = nullptr;
  void* m_display = nullptr;
  void* m_image = nullptr;
  unsigned int m_texture = 0;
  unsigned int m_framebuffer = 0;
  QSize m_size;
  bool m_dmabuf = false;

  using CreateImage = void* (*)(void*, void*, unsigned int, void*, const int*);
  using DestroyImage = unsigned int (*)(void*, void*);
  using ImageTargetTexture = void (*)(unsigned int, void*);
  CreateImage m_createImage = nullptr;
  DestroyImage m_destroyImage = nullptr;
  ImageTargetTexture m_imageTargetTexture = nullptr;
};
