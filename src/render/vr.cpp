#include "render/vr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

#if defined(FA18_OPENXR)
// The platform types of the graphics bindings, without the platforms'
// headers (the handles come from the current context, see current_glx /
// current_egl).
struct _XDisplay;
typedef struct _XDisplay Display;
typedef struct __GLXFBConfigRec* GLXFBConfig;
typedef unsigned long GLXDrawable;
typedef struct __GLXcontextRec* GLXContext;
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef unsigned int EGLenum;
#define XR_USE_PLATFORM_XLIB
#define XR_USE_PLATFORM_EGL
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <dlfcn.h>
#endif

namespace render {
namespace {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLbitfield = unsigned int;
using GLubyte = unsigned char;

enum : GLenum {
  GL_NO_ERROR_ = 0,
  GL_SCISSOR_TEST = 0x0C11,
  GL_TEXTURE_2D = 0x0DE1,
  GL_UNSIGNED_BYTE = 0x1401,
  GL_RGBA = 0x1908,
  GL_VERSION = 0x1F02,
  GL_NEAREST = 0x2600,
  GL_LINEAR = 0x2601,
  GL_TEXTURE_MAG_FILTER = 0x2800,
  GL_TEXTURE_MIN_FILTER = 0x2801,
  GL_TEXTURE_WRAP_S = 0x2802,
  GL_TEXTURE_WRAP_T = 0x2803,
  GL_COLOR_BUFFER_BIT = 0x4000,
  GL_STENCIL_BUFFER_BIT = 0x0400,
  GL_RGBA8 = 0x8058,
  GL_CLAMP_TO_EDGE = 0x812F,
  GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
  GL_DEPTH24_STENCIL8 = 0x88F0,
  GL_SRGB8_ALPHA8 = 0x8C43,
  GL_READ_FRAMEBUFFER = 0x8CA8,
  GL_DRAW_FRAMEBUFFER = 0x8CA9,
  GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
  GL_COLOR_ATTACHMENT0 = 0x8CE0,
  GL_FRAMEBUFFER = 0x8D40,
  GL_RENDERBUFFER = 0x8D41,
  GL_MAX_SAMPLES = 0x8D57,
  GL_FRAMEBUFFER_SRGB = 0x8DB9,
};

#if defined(_WIN32)
#define FA18_GLAPI __stdcall
#else
#define FA18_GLAPI
#endif

struct GL {
  void (FA18_GLAPI* GenFramebuffers)(GLsizei, GLuint*);
  void (FA18_GLAPI* BindFramebuffer)(GLenum, GLuint);
  void (FA18_GLAPI* FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
  void (FA18_GLAPI* FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
  GLenum (FA18_GLAPI* CheckFramebufferStatus)(GLenum);
  void (FA18_GLAPI* GenRenderbuffers)(GLsizei, GLuint*);
  void (FA18_GLAPI* BindRenderbuffer)(GLenum, GLuint);
  void (FA18_GLAPI* RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
  void (FA18_GLAPI* RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
  void (FA18_GLAPI* BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
  void (FA18_GLAPI* GenTextures)(GLsizei, GLuint*);
  void (FA18_GLAPI* BindTexture)(GLenum, GLuint);
  void (FA18_GLAPI* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
  void (FA18_GLAPI* TexParameteri)(GLenum, GLenum, GLint);
  void (FA18_GLAPI* GetIntegerv)(GLenum, GLint*);
  const GLubyte* (FA18_GLAPI* GetString)(GLenum);
  void (FA18_GLAPI* Enable)(GLenum);
  void (FA18_GLAPI* Disable)(GLenum);
  void (FA18_GLAPI* Viewport)(GLint, GLint, GLsizei, GLsizei);
  void (FA18_GLAPI* ClearColor)(float, float, float, float);
  void (FA18_GLAPI* Clear)(GLbitfield);
  void (FA18_GLAPI* ColorMask)(unsigned char, unsigned char, unsigned char, unsigned char);
  GLenum (FA18_GLAPI* GetError)();
};

bool load_gl(GetProc p, GL& gl, std::string& err) {
  std::string m;
  auto get = [&](auto& f, const char* name) {
    f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(p(name));
    if (!f) m += std::string(m.empty() ? "" : ", ") + name;
  };
#define L(n) get(gl.n, "gl" #n)
  L(GenFramebuffers); L(BindFramebuffer); L(FramebufferTexture2D); L(FramebufferRenderbuffer);
  L(CheckFramebufferStatus); L(GenRenderbuffers); L(BindRenderbuffer); L(RenderbufferStorage);
  L(RenderbufferStorageMultisample); L(BlitFramebuffer); L(GenTextures); L(BindTexture);
  L(TexImage2D); L(TexParameteri); L(GetIntegerv); L(GetString); L(Enable); L(Disable);
  L(Viewport); L(ClearColor); L(Clear); L(ColorMask); L(GetError);
#undef L
  if (!m.empty()) {
    err = "the OpenGL context has no framebuffer objects (missing: " + m + ")";
    return false;
  }
  return true;
}

constexpr double kPi = 3.14159265358979323846;

#if defined(FA18_OPENXR)
// A function of the library that made the current context.  SDL loads it
// privately, so it is not among the program's own symbols.
void* gl_library_symbol(const char* name) {
  if (void* p = dlsym(RTLD_DEFAULT, name)) return p;
  for (const char* lib : {"libGLX.so.0", "libGL.so.1", "libEGL.so.1"}) {
    void* h = dlopen(lib, RTLD_LAZY | RTLD_NOLOAD);
    if (!h) continue;
    if (void* p = dlsym(h, name)) return p;
  }
  return nullptr;
}

template <typename F>
F gl_library_function(const char* name) {
  return reinterpret_cast<F>(gl_library_symbol(name));
}

bool current_glx(XrGraphicsBindingOpenGLXlibKHR& b) {
  auto get_context = gl_library_function<GLXContext (*)()>("glXGetCurrentContext");
  auto get_display = gl_library_function<Display* (*)()>("glXGetCurrentDisplay");
  auto get_drawable = gl_library_function<GLXDrawable (*)()>("glXGetCurrentDrawable");
  if (!get_context || !get_display || !get_drawable) return false;
  b = {XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
  b.glxContext = get_context();
  if (!b.glxContext) return false;
  b.xDisplay = get_display();
  b.glxDrawable = get_drawable();
  // the context's framebuffer configuration and its visual
  auto query = gl_library_function<int (*)(Display*, GLXContext, int, int*)>("glXQueryContext");
  auto choose = gl_library_function<GLXFBConfig* (*)(Display*, int, const int*, int*)>("glXChooseFBConfig");
  auto visual = gl_library_function<void* (*)(Display*, GLXFBConfig)>("glXGetVisualFromFBConfig");
  int id = 0, screen = 0, n = 0;
  if (query && choose && query(b.xDisplay, b.glxContext, 0x8013 /* GLX_FBCONFIG_ID */, &id) == 0 &&
      query(b.xDisplay, b.glxContext, 0x800C /* GLX_SCREEN */, &screen) == 0) {
    const int attrs[] = {0x8013, id, 0};
    GLXFBConfig* list = choose(b.xDisplay, screen, attrs, &n);
    if (list && n > 0) {
      b.glxFBConfig = list[0];
      if (visual)
        if (void* info = visual(b.xDisplay, b.glxFBConfig))  // XVisualInfo: Visual*, VisualID, ...
          b.visualid = uint32_t(static_cast<unsigned long*>(info)[1]);
    }
  }
  return true;
}

bool current_egl(XrGraphicsBindingEGLMNDX& b) {
  auto get_context = gl_library_function<EGLContext (*)()>("eglGetCurrentContext");
  auto get_display = gl_library_function<EGLDisplay (*)()>("eglGetCurrentDisplay");
  auto get_proc = gl_library_function<PFN_xrEglGetProcAddressMNDX>("eglGetProcAddress");
  if (!get_context || !get_display || !get_proc) return false;
  b = {XR_TYPE_GRAPHICS_BINDING_EGL_MNDX};
  b.context = get_context();
  if (!b.context) return false;
  b.display = get_display();
  b.getProcAddress = get_proc;
  auto query = gl_library_function<unsigned (*)(EGLDisplay, EGLContext, int, int*)>("eglQueryContext");
  auto choose = gl_library_function<unsigned (*)(EGLDisplay, const int*, EGLConfig*, int, int*)>("eglChooseConfig");
  int id = 0, n = 0;
  if (query && choose && query(b.display, b.context, 0x3028 /* EGL_CONFIG_ID */, &id)) {
    const int attrs[] = {0x3028, id, 0x3038 /* EGL_NONE */};
    choose(b.display, attrs, &b.config, 1, &n);
  }
  return true;
}

std::string xr_error(XrInstance instance, const char* what, XrResult r) {
  char name[XR_MAX_RESULT_STRING_SIZE] = {};
  if (instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(instance, r, name)))
    std::snprintf(name, sizeof name, "%d", int(r));
  return std::string(what) + ": " + name;
}

vr_math::Mat pose_matrix(const XrPosef& p) {
  return vr_math::from_quat(double(p.orientation.x), double(p.orientation.y), double(p.orientation.z),
                            double(p.orientation.w));
}
#endif

}  // namespace

struct Vr::Impl {
  GL gl{};
  Options opt;
  bool sim = true;
  int samples = 0;
  double seat_yaw = 0;
  bool recentre_pending = true;
  std::string description;

  struct Eye {
    int w = 0, h = 0;
    // The finished picture (own texture) and, with multisampling, the
    // buffers it is drawn into first.
    GLuint tex = 0, fbo = 0, ms_fbo = 0, ms_colour = 0, stencil = 0;
#if defined(FA18_OPENXR)
    XrSwapchain chain = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLKHR> images;
    GLuint chain_fbo = 0;
#endif
  } eye[2];

  bool make_eye(Eye& e, int w, int h, std::string& err) {
    e.w = w;
    e.h = h;
    gl.GenTextures(1, &e.tex);
    gl.BindTexture(GL_TEXTURE_2D, e.tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.GenFramebuffers(1, &e.fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, e.fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.tex, 0);
    // the scene keeps its palette indices in the stencil buffer
    gl.GenRenderbuffers(1, &e.stencil);
    gl.BindRenderbuffer(GL_RENDERBUFFER, e.stencil);
    if (samples > 1) {
      gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
      gl.GenRenderbuffers(1, &e.ms_colour);
      gl.BindRenderbuffer(GL_RENDERBUFFER, e.ms_colour);
      gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
      gl.GenFramebuffers(1, &e.ms_fbo);
      gl.BindFramebuffer(GL_FRAMEBUFFER, e.ms_fbo);
      gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, e.ms_colour);
    } else {
      gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    }
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, e.stencil);
    const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) err = "cannot create a framebuffer for an eye's picture";
    return ok;
  }

#if defined(FA18_OPENXR)
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace space = XR_NULL_HANDLE;
  XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  bool running = false, exit_requested = false, frame_begun = false;
  XrTime display_time = 0;
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};

  bool init_xr(std::string& err);
  void poll();
  void end(bool with_layer);
#endif
};

#if defined(FA18_OPENXR)
bool Vr::Impl::init_xr(std::string& err) {
  XrGraphicsBindingOpenGLXlibKHR glx{};
  XrGraphicsBindingEGLMNDX egl{};
  const bool use_glx = current_glx(glx);
  const bool use_egl = !use_glx && current_egl(egl);
  if (!use_glx && !use_egl) {
    err = "the window's OpenGL context is neither GLX nor EGL";
    return false;
  }

  // SteamVR's runtime is not built for this program's library paths: it
  // needs libuuid and libEGL to load at all and looks for the Vulkan loader
  // by name ("failed to find vulkan library" in Steam's
  // logs/xrclient_fa18.txt, XR_ERROR_RUNTIME_FAILURE here).  A library that
  // is already loaded is found by its name, so load them from this
  // program's own search path first (the Nix package has them in its
  // RUNPATH).  Harmless where they are missing or not needed.
  for (const char* lib : {"libuuid.so.1", "libGL.so.1", "libEGL.so.1", "libvulkan.so.1"})
    dlopen(lib, RTLD_NOW | RTLD_GLOBAL);

  uint32_t n = 0;
  XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
  if (XR_FAILED(r)) {
    err = "no OpenXR runtime is active (start SteamVR, Monado or WiVRn, or set XR_RUNTIME_JSON)";
    return false;
  }
  std::vector<XrExtensionProperties> exts(n, {XR_TYPE_EXTENSION_PROPERTIES});
  xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
  auto has = [&](const char* name) {
    return std::any_of(exts.begin(), exts.end(),
                       [&](const XrExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
  };
  std::vector<const char*> enable;
  if (!has(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
    err = "the OpenXR runtime does not support OpenGL (XR_KHR_opengl_enable)";
    return false;
  }
  enable.push_back(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
  if (use_egl) {
    if (!has(XR_MNDX_EGL_ENABLE_EXTENSION_NAME)) {
      err = "the window uses EGL (Wayland?), which this OpenXR runtime does not support "
            "(XR_MNDX_egl_enable); try SDL_VIDEODRIVER=x11";
      return false;
    }
    enable.push_back(XR_MNDX_EGL_ENABLE_EXTENSION_NAME);
  }

  XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
  std::snprintf(ici.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "FA-18 Interceptor");
  ici.applicationInfo.applicationVersion = 1;
  ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  ici.enabledExtensionCount = uint32_t(enable.size());
  ici.enabledExtensionNames = enable.data();
  r = xrCreateInstance(&ici, &instance);
  if (XR_FAILED(r)) {
    err = xr_error(XR_NULL_HANDLE, "xrCreateInstance", r);
    if (r == XR_ERROR_RUNTIME_FAILURE)
      err += " (the runtime failed to start; is it running? SteamVR writes the reason to Steam's "
             "logs/xrclient_fa18.txt)";
    return false;
  }
  XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
  xrGetInstanceProperties(instance, &ip);

  XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
  sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  r = xrGetSystem(instance, &sgi, &system);
  if (XR_FAILED(r)) {
    err = r == XR_ERROR_FORM_FACTOR_UNAVAILABLE ? std::string("no headset is connected (") + ip.runtimeName + ")"
                                                : xr_error(instance, "xrGetSystem", r);
    return false;
  }
  XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
  xrGetSystemProperties(instance, system, &sp);

  // must be asked before a session is created
  PFN_xrGetOpenGLGraphicsRequirementsKHR requirements = nullptr;
  xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
                        reinterpret_cast<PFN_xrVoidFunction*>(&requirements));
  XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
  if (requirements) requirements(instance, system, &req);

  XrViewConfigurationView vcv[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
  r = xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, vcv);
  if (XR_FAILED(r) || n != 2) {
    err = xr_error(instance, "the headset has no stereo view configuration", r);
    return false;
  }
  XrEnvironmentBlendMode modes[4];
  if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                    4, &n, modes)) && n > 0)
    blend = modes[0];

  XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
  sci.next = use_glx ? static_cast<const void*>(&glx) : static_cast<const void*>(&egl);
  sci.systemId = system;
  r = xrCreateSession(instance, &sci, &session);
  if (XR_FAILED(r)) {
    err = xr_error(instance, "xrCreateSession", r);
    if (XR_VERSION_MAJOR(req.minApiVersionSupported) > 0) {
      const char* ver = reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
      err += " (the runtime asks for OpenGL " + std::to_string(XR_VERSION_MAJOR(req.minApiVersionSupported)) + "." +
             std::to_string(XR_VERSION_MINOR(req.minApiVersionSupported)) + ", the window has " + (ver ? ver : "?") + ")";
    }
    return false;
  }

  XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  rsci.poseInReferenceSpace.orientation.w = 1;
  r = xrCreateReferenceSpace(session, &rsci, &space);
  if (XR_FAILED(r)) {
    err = xr_error(instance, "xrCreateReferenceSpace", r);
    return false;
  }

  // The palette's colours are sRGB values and are written as they are.
  r = xrEnumerateSwapchainFormats(session, 0, &n, nullptr);
  std::vector<int64_t> formats(n);
  if (XR_SUCCEEDED(r)) r = xrEnumerateSwapchainFormats(session, n, &n, formats.data());
  if (XR_FAILED(r) || formats.empty()) {
    err = xr_error(instance, "xrEnumerateSwapchainFormats", r);
    return false;
  }
  int64_t format = formats[0];
  for (int64_t want : {int64_t(GL_RGBA8), int64_t(GL_SRGB8_ALPHA8)})
    if (std::find(formats.begin(), formats.end(), want) != formats.end()) format = want;
  if (format != GL_SRGB8_ALPHA8)
    std::fprintf(stderr, "vr: the runtime offers no sRGB picture format (using 0x%llx); colours may be off\n",
                 (unsigned long long)format);

  for (int i = 0; i < 2; ++i) {
    Eye& e = eye[i];
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = vcv[i].recommendedImageRectWidth;
    ci.height = vcv[i].recommendedImageRectHeight;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    r = xrCreateSwapchain(session, &ci, &e.chain);
    if (XR_SUCCEEDED(r)) r = xrEnumerateSwapchainImages(e.chain, 0, &n, nullptr);
    if (XR_SUCCEEDED(r)) {
      e.images.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
      r = xrEnumerateSwapchainImages(e.chain, n, &n,
                                     reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data()));
    }
    if (XR_FAILED(r)) {
      err = xr_error(instance, "xrCreateSwapchain", r);
      return false;
    }
    gl.GenFramebuffers(1, &e.chain_fbo);
    if (!make_eye(e, int(ci.width), int(ci.height), err)) return false;
  }
  description = std::string("OpenXR, ") + ip.runtimeName + ", " + sp.systemName + ", " + std::to_string(eye[0].w) +
                "x" + std::to_string(eye[0].h) + " per eye, " + (use_glx ? "GLX" : "EGL");
  sim = false;
  return true;
}

void Vr::Impl::poll() {
  XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(instance, &ev) == XR_SUCCESS) {
    if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      const auto& sc = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
      if (sc.state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        running = XR_SUCCEEDED(xrBeginSession(session, &bi));
      } else if (sc.state == XR_SESSION_STATE_STOPPING) {
        running = false;
        xrEndSession(session);
      } else if (sc.state == XR_SESSION_STATE_EXITING || sc.state == XR_SESSION_STATE_LOSS_PENDING) {
        running = false;
        exit_requested = true;
      }
    } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      running = false;
      exit_requested = true;
    }
    ev = {XR_TYPE_EVENT_DATA_BUFFER};
  }
}

void Vr::Impl::end(bool with_layer) {
  if (!frame_begun) return;
  frame_begun = false;
  XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  for (int i = 0; i < 2; ++i) {
    pv[i].pose = views[i].pose;
    pv[i].fov = views[i].fov;
    pv[i].subImage.swapchain = eye[i].chain;
    pv[i].subImage.imageRect.extent = {eye[i].w, eye[i].h};
  }
  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  layer.space = space;
  layer.viewCount = 2;
  layer.views = pv;
  const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
  XrFrameEndInfo fe{XR_TYPE_FRAME_END_INFO};
  fe.displayTime = display_time;
  fe.environmentBlendMode = blend;
  fe.layerCount = with_layer ? 1 : 0;
  fe.layers = with_layer ? layers : nullptr;
  xrEndFrame(session, &fe);
}
#endif

Vr::Vr() : p_(std::make_unique<Impl>()) {}

Vr::~Vr() { shutdown(); }

void Vr::shutdown() {
#if defined(FA18_OPENXR)
  Impl& s = *p_;
  s.running = false;
  if (s.session != XR_NULL_HANDLE) xrDestroySession(s.session);
  if (s.instance != XR_NULL_HANDLE) xrDestroyInstance(s.instance);
  s.session = XR_NULL_HANDLE;
  s.instance = XR_NULL_HANDLE;
  s.sim = true;
#endif
}

bool Vr::has_openxr() {
#if defined(FA18_OPENXR)
  return true;
#else
  return false;
#endif
}

bool Vr::init(GetProc proc, const Options& opt, std::string& err) {
  Impl& s = *p_;
  s.opt = opt;
  if (!load_gl(proc, s.gl, err)) return false;
  const char* version = reinterpret_cast<const char*>(s.gl.GetString(GL_VERSION));
  if (version && std::strncmp(version, "OpenGL ES", 9) == 0) {
    err = "needs desktop OpenGL (the window has " + std::string(version) + ")";
    return false;
  }
  GLint max_samples = 0;
  s.gl.GetIntegerv(GL_MAX_SAMPLES, &max_samples);
  while (s.gl.GetError() != GL_NO_ERROR_) {}
  s.samples = std::clamp(opt.samples, 0, std::max(0, int(max_samples)));
  if (s.samples < 2) s.samples = 0;
  if (opt.simulate) {
    const int size = std::clamp(opt.eye_size, 64, 8192);
    for (Impl::Eye& e : s.eye)
      if (!s.make_eye(e, size, size, err)) return false;
    char text[160];
    std::snprintf(text, sizeof text, "simulated headset, head %g right, %g up, %g clockwise, %dx%d per eye",
                  double(opt.yaw), double(opt.pitch), double(opt.roll), size, size);
    s.description = text;
    s.sim = true;
    return true;
  }
#if defined(FA18_OPENXR)
  return s.init_xr(err);
#else
  err = "this build has no OpenXR support (configure with the OpenXR loader installed)";
  return false;
#endif
}

bool Vr::simulated() const { return p_->sim; }
std::string Vr::description() const {
  return p_->description + (p_->samples > 1 ? ", " + std::to_string(p_->samples) + "x multisampling" : "");
}

Vr::Step Vr::begin_frame(HeadView& head) {
  Impl& s = *p_;
  vr_math::Mat pose[2];
  double fov[2][4];
  const int size[2][2] = {{s.eye[0].w, s.eye[0].h}, {s.eye[1].w, s.eye[1].h}};
  if (s.sim) {
    const double k = kPi / 180.0, half = std::clamp(double(s.opt.fov), 10.0, 170.0) * k / 2;
    for (int i = 0; i < 2; ++i) {
      pose[i] = vr_math::from_angles(double(s.opt.yaw) * k, double(s.opt.pitch) * k, double(s.opt.roll) * k);
      fov[i][0] = -half, fov[i][1] = half, fov[i][2] = half, fov[i][3] = -half;
    }
    head = vr_math::head_view(pose, fov, size, 0.0);
    return Step::Render;
  }
#if defined(FA18_OPENXR)
  s.poll();
  if (!s.running || s.exit_requested) return Step::Idle;
  XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState fs{XR_TYPE_FRAME_STATE};
  if (XR_FAILED(xrWaitFrame(s.session, &wi, &fs))) return Step::Idle;
  XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
  if (XR_FAILED(xrBeginFrame(s.session, &bi))) return Step::Idle;
  s.frame_begun = true;
  s.display_time = fs.predictedDisplayTime;
  XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
  li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  li.displayTime = s.display_time;
  li.space = s.space;
  XrViewState vs{XR_TYPE_VIEW_STATE};
  uint32_t n = 0;
  s.views[0] = {XR_TYPE_VIEW};
  s.views[1] = {XR_TYPE_VIEW};
  if (!fs.shouldRender || XR_FAILED(xrLocateViews(s.session, &li, &vs, 2, &n, s.views)) || n != 2 ||
      !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
    s.end(false);
    return Step::Skip;
  }
  for (int i = 0; i < 2; ++i) {
    pose[i] = pose_matrix(s.views[i].pose);
    fov[i][0] = double(s.views[i].fov.angleLeft);
    fov[i][1] = double(s.views[i].fov.angleRight);
    fov[i][2] = double(s.views[i].fov.angleUp);
    fov[i][3] = double(s.views[i].fov.angleDown);
  }
  if (s.recentre_pending) {
    s.seat_yaw = vr_math::yaw_of(vr_math::midway(pose));
    s.recentre_pending = false;
  }
  head = vr_math::head_view(pose, fov, size, s.seat_yaw);
  return Step::Render;
#else
  return Step::Idle;
#endif
}

void Vr::begin_eye(int i) {
  Impl& s = *p_;
  const Impl::Eye& e = s.eye[i & 1];
  s.gl.BindFramebuffer(GL_FRAMEBUFFER, e.ms_fbo ? e.ms_fbo : e.fbo);
  s.gl.Disable(GL_FRAMEBUFFER_SRGB);
}

void Vr::end_eye(int i) {
  Impl& s = *p_;
  const GL& gl = s.gl;
  Impl::Eye& e = s.eye[i & 1];
  gl.Disable(GL_SCISSOR_TEST);
  gl.ColorMask(1, 1, 1, 1);
  if (e.ms_fbo) {
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, e.ms_fbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, e.fbo);
    gl.BlitFramebuffer(0, 0, e.w, e.h, 0, 0, e.w, e.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
#if defined(FA18_OPENXR)
  if (!s.sim && s.frame_begun) {
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    if (XR_SUCCEEDED(xrAcquireSwapchainImage(e.chain, &ai, &index))) {
      XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
      wi.timeout = XR_INFINITE_DURATION;
      if (XR_SUCCEEDED(xrWaitSwapchainImage(e.chain, &wi)) && index < e.images.size()) {
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, e.fbo);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, e.chain_fbo);
        gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e.images[index].image, 0);
        gl.Disable(GL_FRAMEBUFFER_SRGB);
        gl.BlitFramebuffer(0, 0, e.w, e.h, 0, 0, e.w, e.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
      }
      XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(e.chain, &ri);
    }
  }
#endif
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Vr::end_frame() {
#if defined(FA18_OPENXR)
  if (!p_->sim) p_->end(true);
#endif
}

void Vr::mirror(int win_w, int win_h) {
  Impl& s = *p_;
  const GL& gl = s.gl;
  gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  gl.Disable(GL_SCISSOR_TEST);
  gl.ColorMask(1, 1, 1, 1);
  gl.Viewport(0, 0, win_w, win_h);
  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  if (s.sim) {
    // both eyes side by side, whole
    for (int i = 0; i < 2; ++i) {
      const Impl::Eye& e = s.eye[i];
      const int cell_w = win_w / 2;
      const double k = std::min(double(cell_w) / e.w, double(win_h) / e.h);
      const int w = int(e.w * k), h = int(e.h * k);
      const int x = i * cell_w + (cell_w - w) / 2, y = (win_h - h) / 2;
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, e.fbo);
      gl.BlitFramebuffer(0, 0, e.w, e.h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
  } else {
    // the middle of the left eye's picture, filling the window
    const Impl::Eye& e = s.eye[0];
    const double k = std::max(double(win_w) / e.w, double(win_h) / e.h);
    const int w = std::min(e.w, int(win_w / k)), h = std::min(e.h, int(win_h / k));
    const int x = (e.w - w) / 2, y = (e.h - h) / 2;
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, e.fbo);
    gl.BlitFramebuffer(x, y, x + w, y + h, 0, 0, win_w, win_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  }
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Vr::paced() const {
#if defined(FA18_OPENXR)
  return !p_->sim && p_->running && !p_->exit_requested;
#else
  return false;
#endif
}

bool Vr::exit_requested() const {
#if defined(FA18_OPENXR)
  return p_->exit_requested;
#else
  return false;
#endif
}

void Vr::recentre() { p_->recentre_pending = true; }

}  // namespace render
