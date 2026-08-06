#include "FramePresenter.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QtGlobal>

#if QT_CONFIG(egl)
#define EGL_NO_X11
#define MESA_EGL_NO_X11_HEADERS
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#include <array>
#include <cstdint>

namespace {

constexpr uint64_t kInvalidModifier = 0x00ffffffffffffffULL;

}  // namespace

void* FramePresenter::eglDisplay(QOpenGLContext* context) {
#if QT_CONFIG(egl)
  if (context == nullptr) {
    return nullptr;
  }
  auto* egl = context->nativeInterface<QNativeInterface::QEGLContext>();
  return egl == nullptr ? nullptr : egl->display();
#else
  Q_UNUSED(context);
  return nullptr;
#endif
}

bool FramePresenter::initialize(QOpenGLContext* context) {
  m_context = context;
  QOpenGLExtraFunctions* gl = context->extraFunctions();
  gl->glGenTextures(1, &m_texture);
  gl->glBindTexture(GL_TEXTURE_2D, m_texture);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  gl->glGenFramebuffers(1, &m_framebuffer);

#if QT_CONFIG(egl)
  m_display = eglDisplay(context);
  if (m_display == nullptr) {
    return false;
  }
  const char* extensions =
      eglQueryString(static_cast<EGLDisplay>(m_display), EGL_EXTENSIONS);
  if (extensions == nullptr ||
      !QByteArray(extensions).contains("EGL_EXT_image_dma_buf_import") ||
      !context->hasExtension("GL_OES_EGL_image")) {
    return false;
  }
  m_createImage =
      reinterpret_cast<CreateImage>(eglGetProcAddress("eglCreateImageKHR"));
  m_destroyImage =
      reinterpret_cast<DestroyImage>(eglGetProcAddress("eglDestroyImageKHR"));
  m_imageTargetTexture = reinterpret_cast<ImageTargetTexture>(
      context->getProcAddress("glEGLImageTargetTexture2DOES"));
  return m_createImage != nullptr && m_destroyImage != nullptr &&
         m_imageTargetTexture != nullptr;
#else
  return false;
#endif
}

void FramePresenter::release() {
  if (m_context == nullptr) {
    return;
  }

  destroyImage();
  QOpenGLExtraFunctions* gl = m_context->extraFunctions();
  gl->glDeleteFramebuffers(1, &m_framebuffer);
  gl->glDeleteTextures(1, &m_texture);
  m_framebuffer = 0;
  m_texture = 0;
  m_size = {};
  m_context = nullptr;
}

bool FramePresenter::update(ghostty_frame_s& frame) {
  bool imported = true;
  if (m_context != nullptr) {
    if (frame.type == GHOSTTY_FRAME_DMABUF) {
      imported = importDmabuf(frame);
    } else {
      uploadMemory(frame);
    }
  }
  ghostty_frame_free(&frame);
  return imported;
}

void FramePresenter::paint(unsigned int framebuffer, QSize size) {
  QOpenGLExtraFunctions* gl = m_context->extraFunctions();
  gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
  gl->glClearColor(0, 0, 0, 0);
  gl->glClear(GL_COLOR_BUFFER_BIT);
  if (m_size.isEmpty()) {
    return;
  }

  // OpenGL rows start at the bottom. Keep the frame at the top-left corner
  // while the size of the frame and the widget differ.
  const int width = qMin(m_size.width(), size.width());
  const int height = qMin(m_size.height(), size.height());
  const int sourceY = m_size.height() - height;
  const int targetY = size.height() - height;
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
  gl->glBlitFramebuffer(0, sourceY, width, sourceY + height, 0, targetY, width,
                        targetY + height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
}

bool FramePresenter::importDmabuf(const ghostty_frame_s& frame) {
#if QT_CONFIG(egl)
  if (m_createImage == nullptr || frame.plane_count == 0 ||
      frame.plane_count > 4) {
    return false;
  }

  static constexpr std::array<EGLint, 4> fdKeys = {
      EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT,
      EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT};
  static constexpr std::array<EGLint, 4> offsetKeys = {
      EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
      EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
  static constexpr std::array<EGLint, 4> pitchKeys = {
      EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
      EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
  static constexpr std::array<EGLint, 4> modifierLowKeys = {
      EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
      EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
  static constexpr std::array<EGLint, 4> modifierHighKeys = {
      EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
      EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};

  std::array<EGLint, 64> attributes{};
  size_t count = 0;
  const auto add = [&](EGLint key, EGLint value) {
    attributes[count++] = key;
    attributes[count++] = value;
  };
  add(EGL_WIDTH, static_cast<EGLint>(frame.width));
  add(EGL_HEIGHT, static_cast<EGLint>(frame.height));
  add(EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(frame.fourcc));
  for (uint32_t plane = 0; plane < frame.plane_count; ++plane) {
    add(fdKeys[plane], frame.fds[plane]);
    add(offsetKeys[plane], frame.offsets[plane]);
    add(pitchKeys[plane], frame.strides[plane]);
    if (frame.modifier != kInvalidModifier) {
      add(modifierLowKeys[plane],
          static_cast<EGLint>(frame.modifier & 0xffffffffU));
      add(modifierHighKeys[plane], static_cast<EGLint>(frame.modifier >> 32));
    }
  }
  attributes[count] = EGL_NONE;

  void* image = m_createImage(m_display, EGL_NO_CONTEXT,
                              EGL_LINUX_DMA_BUF_EXT, nullptr,
                              attributes.data());
  if (image == EGL_NO_IMAGE_KHR) {
    return false;
  }

  QOpenGLExtraFunctions* gl = m_context->extraFunctions();
  gl->glBindTexture(GL_TEXTURE_2D, m_texture);
  m_imageTargetTexture(GL_TEXTURE_2D, image);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  destroyImage();
  m_image = image;
  m_dmabuf = true;
  attachTexture(QSize(static_cast<int>(frame.width),
                      static_cast<int>(frame.height)));
  return true;
#else
  Q_UNUSED(frame);
  return false;
#endif
}

void FramePresenter::uploadMemory(const ghostty_frame_s& frame) {
  if (frame.pixels == nullptr ||
      frame.pixels_len < size_t{frame.width} * frame.height * 4) {
    return;
  }

  QOpenGLExtraFunctions* gl = m_context->extraFunctions();
  if (m_dmabuf) {
    // A texture that holds an EGL image cannot get new storage.
    destroyImage();
    gl->glDeleteTextures(1, &m_texture);
    gl->glGenTextures(1, &m_texture);
    m_dmabuf = false;
  }
  gl->glBindTexture(GL_TEXTURE_2D, m_texture);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(frame.width),
                   static_cast<int>(frame.height), 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, frame.pixels);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  attachTexture(QSize(static_cast<int>(frame.width),
                      static_cast<int>(frame.height)));
}

void FramePresenter::attachTexture(QSize size) {
  QOpenGLExtraFunctions* gl = m_context->extraFunctions();
  GLint previous = 0;
  gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
  gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, m_texture, 0);
  const bool complete = gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) ==
                        GL_FRAMEBUFFER_COMPLETE;
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous));
  m_size = complete ? size : QSize();
}

void FramePresenter::destroyImage() {
  if (m_image != nullptr && m_destroyImage != nullptr) {
    m_destroyImage(m_display, m_image);
  }
  m_image = nullptr;
}
