#include "render/gl_scene.hpp"

#include "native/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace render {
namespace {

// ---- the GL subset used (GL 2.1 and GLES 2 share it) ---------------------------

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLsizeiptr = std::ptrdiff_t;
using GLchar = char;
using GLubyte = unsigned char;

enum : GLenum {
  GL_TRIANGLES = 0x0004,
  GL_ONE = 1,
  GL_ONE_MINUS_SRC_ALPHA = 0x0303,
  GL_EQUAL = 0x0202,
  GL_ALWAYS = 0x0207,
  GL_BLEND = 0x0BE2,
  GL_CULL_FACE = 0x0B44,
  GL_DEPTH_TEST = 0x0B71,
  GL_STENCIL_TEST = 0x0B90,
  GL_SCISSOR_TEST = 0x0C11,
  GL_STENCIL_BITS = 0x0D57,
  GL_TEXTURE_2D = 0x0DE1,
  GL_UNSIGNED_BYTE = 0x1401,
  GL_FLOAT = 0x1406,
  GL_INVERT = 0x150A,
  GL_RGB = 0x1907,
  GL_RGBA = 0x1908,
  GL_KEEP = 0x1E00,
  GL_REPLACE = 0x1E01,
  GL_NEAREST = 0x2600,
  GL_LINEAR = 0x2601,
  GL_TEXTURE_MAG_FILTER = 0x2800,
  GL_TEXTURE_MIN_FILTER = 0x2801,
  GL_TEXTURE_WRAP_S = 0x2802,
  GL_TEXTURE_WRAP_T = 0x2803,
  GL_CLAMP_TO_EDGE = 0x812F,
  GL_TEXTURE0 = 0x84C0,
  GL_ARRAY_BUFFER = 0x8892,
  GL_STREAM_DRAW = 0x88E0,
  GL_FRAGMENT_SHADER = 0x8B30,
  GL_VERTEX_SHADER = 0x8B31,
  GL_COMPILE_STATUS = 0x8B81,
  GL_LINK_STATUS = 0x8B82,
  GL_PACK_ALIGNMENT = 0x0D05,
  GL_UNPACK_ALIGNMENT = 0x0CF5,
  GL_COLOR_BUFFER_BIT = 0x4000,
  GL_STENCIL_BUFFER_BIT = 0x0400,
};

#if defined(_WIN32)
#define FA18_GLAPI __stdcall
#else
#define FA18_GLAPI
#endif

struct GL {
  void (FA18_GLAPI* Viewport)(GLint, GLint, GLsizei, GLsizei);
  void (FA18_GLAPI* Scissor)(GLint, GLint, GLsizei, GLsizei);
  void (FA18_GLAPI* Enable)(GLenum);
  void (FA18_GLAPI* Disable)(GLenum);
  void (FA18_GLAPI* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
  void (FA18_GLAPI* ClearStencil)(GLint);
  void (FA18_GLAPI* Clear)(GLbitfield);
  void (FA18_GLAPI* ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
  void (FA18_GLAPI* DepthMask)(GLboolean);
  void (FA18_GLAPI* StencilFunc)(GLenum, GLint, GLuint);
  void (FA18_GLAPI* StencilOp)(GLenum, GLenum, GLenum);
  void (FA18_GLAPI* StencilMask)(GLuint);
  void (FA18_GLAPI* BlendFunc)(GLenum, GLenum);
  GLuint (FA18_GLAPI* CreateShader)(GLenum);
  void (FA18_GLAPI* ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
  void (FA18_GLAPI* CompileShader)(GLuint);
  void (FA18_GLAPI* GetShaderiv)(GLuint, GLenum, GLint*);
  void (FA18_GLAPI* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
  GLuint (FA18_GLAPI* CreateProgram)();
  void (FA18_GLAPI* AttachShader)(GLuint, GLuint);
  void (FA18_GLAPI* BindAttribLocation)(GLuint, GLuint, const GLchar*);
  void (FA18_GLAPI* LinkProgram)(GLuint);
  void (FA18_GLAPI* GetProgramiv)(GLuint, GLenum, GLint*);
  void (FA18_GLAPI* UseProgram)(GLuint);
  GLint (FA18_GLAPI* GetUniformLocation)(GLuint, const GLchar*);
  void (FA18_GLAPI* Uniform1i)(GLint, GLint);
  void (FA18_GLAPI* Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
  void (FA18_GLAPI* Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
  void (FA18_GLAPI* UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
  void (FA18_GLAPI* GenBuffers)(GLsizei, GLuint*);
  void (FA18_GLAPI* BindBuffer)(GLenum, GLuint);
  void (FA18_GLAPI* BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
  void (FA18_GLAPI* EnableVertexAttribArray)(GLuint);
  void (FA18_GLAPI* DisableVertexAttribArray)(GLuint);
  void (FA18_GLAPI* VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
  void (FA18_GLAPI* DrawArrays)(GLenum, GLint, GLsizei);
  void (FA18_GLAPI* GenTextures)(GLsizei, GLuint*);
  void (FA18_GLAPI* BindTexture)(GLenum, GLuint);
  void (FA18_GLAPI* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                const void*);
  void (FA18_GLAPI* TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                                   const void*);
  void (FA18_GLAPI* TexParameteri)(GLenum, GLenum, GLint);
  void (FA18_GLAPI* ActiveTexture)(GLenum);
  void (FA18_GLAPI* PixelStorei)(GLenum, GLint);
  void (FA18_GLAPI* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
  void (FA18_GLAPI* GetIntegerv)(GLenum, GLint*);
  const GLubyte* (FA18_GLAPI* GetString)(GLenum);
};

template <typename F>
void load(GetProc proc, F& f, const char* name, std::string& missing) {
  f = reinterpret_cast<F>(proc(name));
  if (!f) missing += std::string(missing.empty() ? "" : ", ") + name;
}

bool load_gl(GetProc p, GL& gl, std::string& err) {
  std::string m;
#define L(n) load(p, gl.n, "gl" #n, m)
  L(Viewport); L(Scissor); L(Enable); L(Disable); L(ClearColor); L(ClearStencil); L(Clear);
  L(ColorMask); L(DepthMask); L(StencilFunc); L(StencilOp); L(StencilMask); L(BlendFunc);
  L(CreateShader); L(ShaderSource); L(CompileShader); L(GetShaderiv); L(GetShaderInfoLog);
  L(CreateProgram); L(AttachShader); L(BindAttribLocation); L(LinkProgram); L(GetProgramiv);
  L(UseProgram); L(GetUniformLocation); L(Uniform1i); L(Uniform3f); L(Uniform4f);
  L(UniformMatrix3fv); L(GenBuffers); L(BindBuffer); L(BufferData); L(EnableVertexAttribArray);
  L(DisableVertexAttribArray); L(VertexAttribPointer); L(DrawArrays); L(GenTextures);
  L(BindTexture); L(TexImage2D); L(TexSubImage2D); L(TexParameteri); L(ActiveTexture);
  L(PixelStorei); L(ReadPixels); L(GetIntegerv); L(GetString);
#undef L
  if (!m.empty()) {
    err = "missing OpenGL functions: " + m;
    return false;
  }
  return true;
}

// ---- shaders -------------------------------------------------------------------
// Scene: v = u_m (a_pos + u_off) is view space (see display_list.hpp), and
// u_clip takes it to clip space (x, y, w).  For the window that is the
// original's projection, columns 159 - 160 x/z and rows 89 - 90 y/z of a
// 320x200 screen (x = -x/z - half a pixel, y = 0.105 + 0.9 y/z); for an eye
// of a headset its own projection.  A constant z of 0 leaves only the w > 0
// side of the frustum to clip against.

const char* kSceneVertex = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec3 a_pos;
uniform mat3 u_m;
uniform vec3 u_off;
uniform mat3 u_clip;
void main() {
  vec3 c = u_clip * (u_m * (a_pos + u_off));
  gl_Position = vec4(c.xy, 0.0, c.z);
}
)";

const char* kSceneFragment = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform vec4 u_colour;
void main() { gl_FragColor = u_colour; }
)";

const char* kImageVertex = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec3 a_pos;
attribute vec2 a_uv;
uniform mat3 u_clip;
varying vec2 v_uv;
void main() {
  v_uv = a_uv;
  vec3 c = u_clip * a_pos;
  gl_Position = vec4(c.xy, 0.0, c.z);
}
)";

const char* kImageFragment = R"(
#ifdef GL_ES
precision mediump float;
#endif
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#endif
uniform sampler2D u_image;
uniform vec4 u_tex;  // texture size, output pixels per texel
varying vec2 v_uv;
void main() {
  // Enlarged pixels stay sharp: blend only across the last output pixel
  // between two texels (the texture is filtered linearly).
  vec2 p = v_uv * u_tex.xy - 0.5;
  vec2 i = floor(p);
  vec2 f = clamp((p - i - 0.5) * u_tex.zw + 0.5, 0.0, 1.0);
  gl_FragColor = texture2D(u_image, (i + f + 0.5) / u_tex.xy);
}
)";

struct V3 {
  double x, y, z;
};

V3 xform(const std::array<float, 9>& m, const Vec3& p) {
  return {double(m[0]) * p.x + double(m[1]) * p.y + double(m[2]) * p.z,
          double(m[3]) * p.x + double(m[4]) * p.y + double(m[5]) * p.z,
          double(m[6]) * p.x + double(m[7]) * p.y + double(m[8]) * p.z};
}

// Ear clipping of a planar polygon given in 2D; indices into `p`.
void ear_clip(const std::vector<std::pair<double, double>>& p, std::vector<int>& tris) {
  const int n = int(p.size());
  if (n < 3) return;
  std::vector<int> idx(size_t(n), 0);
  double area = 0;
  for (int i = 0; i < n; ++i) {
    idx[size_t(i)] = i;
    const auto &a = p[size_t(i)], &b = p[size_t((i + 1) % n)];
    area += a.first * b.second - b.first * a.second;
  }
  const double sign = area < 0 ? -1.0 : 1.0;
  auto cross = [&](int a, int b, int c) {
    return sign * ((p[size_t(b)].first - p[size_t(a)].first) * (p[size_t(c)].second - p[size_t(a)].second) -
                   (p[size_t(b)].second - p[size_t(a)].second) * (p[size_t(c)].first - p[size_t(a)].first));
  };
  for (int guard = 0; idx.size() > 3 && guard < 4096; ++guard) {
    bool clipped = false;
    const int m = int(idx.size());
    for (int i = 0; i < m; ++i) {
      const int a = idx[size_t((i + m - 1) % m)], b = idx[size_t(i)], c = idx[size_t((i + 1) % m)];
      if (cross(a, b, c) <= 0) continue;
      bool inside = false;
      for (int k : idx) {
        if (k == a || k == b || k == c) continue;
        if (cross(a, b, k) > 0 && cross(b, c, k) > 0 && cross(c, a, k) > 0) {
          inside = true;
          break;
        }
      }
      if (inside) continue;
      tris.insert(tris.end(), {a, b, c});
      idx.erase(idx.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) break;  // degenerate: fan the rest
  }
  for (size_t i = 1; i + 1 < idx.size(); ++i) tris.insert(tris.end(), {idx[0], idx[i], idx[i + 1]});
}

struct Batch {
  PlaneOp op;
  GLint first = 0;
  GLsizei count = 0;
};

}  // namespace

struct GlScene::Impl {
  GL gl{};
  GLuint scene_prog = 0, image_prog = 0, vbo = 0;
  GLint u_m = -1, u_off = -1, u_clip = -1, u_colour = -1, u_image = -1, u_tex = -1, u_image_clip = -1;
  struct Tex {
    GLuint id = 0;
    int w = 0, h = 0;
  };
  Tex tex[4];
  int win_w = 0, win_h = 0;
  struct WaterTri {
    double x[3], z[3];
    int32_t min_alt;
  };
  std::vector<WaterTri> water;
  std::vector<float> buf;
  std::vector<Batch> batches;
  std::vector<uint8_t> rgba;
  Stats stats;

  // How a scene is projected: the window (the original's projection) or one
  // eye of a headset.
  struct Target {
    GLint x = 0, y = 0, w = 0, h = 0;  // viewport, GL window coordinates
    // Size in output pixels of the picture that the original's projection
    // of this frame (clip -1..1) would make: sets the stroke of the lines.
    double vw = 0, vh = 0;
    float clip[9] = {};                // view space -> clip (x, y, w), column-major
    // A headset's eye: where it is from the camera, in world units along
    // the axes of the frame's view space (unscaled), and the scales from the
    // frame's view space to those axes (x widen / s0, y heighten / s1, z / s2).
    bool offset = false;
    double eye[3] = {}, axis[3] = {1, 1, 1};
  };
  void scene(const Frame& f, const uint32_t* palette, const Target& t);
  void upload(int slot, const uint32_t* argb, int w, int h, bool blend);
  void image(int slot, const float* quad, const float* clip, float sx, float sy, bool blend);
  // Projection of one eye as a matrix on eye-space vectors (x left, y up,
  // z forward), row-major.
  static void eye_projection(const EyeView& e, double out[9]);

  GLuint compile(GLenum type, const char* src, std::string& err) {
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &src, nullptr);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[512] = {};
      gl.GetShaderInfoLog(s, sizeof log - 1, nullptr, log);
      err = std::string("shader: ") + log;
      return 0;
    }
    return s;
  }
  GLuint link(const char* vs, const char* fs, bool uv, std::string& err) {
    const GLuint v = compile(GL_VERTEX_SHADER, vs, err);
    const GLuint f = v ? compile(GL_FRAGMENT_SHADER, fs, err) : 0;
    if (!v || !f) return 0;
    const GLuint p = gl.CreateProgram();
    gl.AttachShader(p, v);
    gl.AttachShader(p, f);
    gl.BindAttribLocation(p, 0, "a_pos");
    if (uv) gl.BindAttribLocation(p, 1, "a_uv");
    gl.LinkProgram(p);
    GLint ok = 0;
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
      err = "shader program does not link";
      return 0;
    }
    return p;
  }

  void push(double x, double y, double z) {
    buf.push_back(float(x));
    buf.push_back(float(y));
    buf.push_back(float(z));
  }
  void batch(const PlaneOp& op, size_t first_float) {
    const GLint first = GLint(first_float / 3);
    const GLsizei count = GLsizei(buf.size() / 3) - first;
    if (count <= 0) return;
    if (!batches.empty() && batches.back().op == op &&
        batches.back().first + batches.back().count == first)
      batches.back().count += count;
    else
      batches.push_back({op, first, count});
  }
  void apply(const Batch& b) {
    if (b.op.copy3to2) {
      gl.StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
      gl.StencilMask(4);
      gl.StencilFunc(GL_EQUAL, 12, 8);
      gl.DrawArrays(GL_TRIANGLES, b.first, b.count);
      gl.StencilFunc(GL_EQUAL, 0, 8);
      gl.DrawArrays(GL_TRIANGLES, b.first, b.count);
      return;
    }
    if (b.op.set | b.op.clr) {
      gl.StencilFunc(GL_ALWAYS, b.op.set, 0xFF);
      gl.StencilMask(GLuint(b.op.set | b.op.clr));
      gl.StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
      gl.DrawArrays(GL_TRIANGLES, b.first, b.count);
    }
    if (b.op.xr) {
      gl.StencilFunc(GL_ALWAYS, 0, 0xFF);
      gl.StencilMask(b.op.xr);
      gl.StencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
      gl.DrawArrays(GL_TRIANGLES, b.first, b.count);
    }
  }
};

GlScene::GlScene() : p_(std::make_unique<Impl>()) {}
GlScene::~GlScene() = default;

bool GlScene::init(GetProc proc, std::string& err) {
  Impl& s = *p_;
  if (!load_gl(proc, s.gl, err)) return false;
  GLint bits = 0;
  s.gl.GetIntegerv(GL_STENCIL_BITS, &bits);
  if (bits < 5) {
    err = "the OpenGL window has no stencil buffer";
    return false;
  }
  s.scene_prog = s.link(kSceneVertex, kSceneFragment, false, err);
  s.image_prog = s.scene_prog ? s.link(kImageVertex, kImageFragment, true, err) : 0;
  if (!s.scene_prog || !s.image_prog) return false;
  s.u_m = s.gl.GetUniformLocation(s.scene_prog, "u_m");
  s.u_off = s.gl.GetUniformLocation(s.scene_prog, "u_off");
  s.u_clip = s.gl.GetUniformLocation(s.scene_prog, "u_clip");
  s.u_colour = s.gl.GetUniformLocation(s.scene_prog, "u_colour");
  s.u_image = s.gl.GetUniformLocation(s.image_prog, "u_image");
  s.u_tex = s.gl.GetUniformLocation(s.image_prog, "u_tex");
  s.u_image_clip = s.gl.GetUniformLocation(s.image_prog, "u_clip");
  s.gl.GenBuffers(1, &s.vbo);
  return true;
}

void GlScene::set_terrain(const terrain::Terrain& t) {
  Impl& s = *p_;
  s.water.clear();
  auto add_tile = [&](const terrain::Tile& tile) {
    for (const terrain::Polygon& poly : tile.polys) {
      std::vector<std::pair<double, double>> v;
      for (auto& [x, z] : poly.v) v.push_back({double(x), double(z)});
      v.erase(std::unique(v.begin(), v.end()), v.end());
      if (v.size() > 1 && v.front() == v.back()) v.pop_back();
      std::vector<int> tris;
      ear_clip(v, tris);
      for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        Impl::WaterTri w;
        for (int k = 0; k < 3; ++k) {
          w.x[k] = v[size_t(tris[i + size_t(k)])].first;
          w.z[k] = v[size_t(tris[i + size_t(k)])].second;
        }
        w.min_alt = poly.min_altitude;
        s.water.push_back(w);
      }
    }
  };
  // Inside the map (far tiles 4-5) the near grid is the detailed version.
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c)
      if (!(r >= 4 && r <= 5 && c >= 4 && c <= 5)) add_tile(t.far[r][c]);
  for (int r = 0; r < 32; ++r)
    for (int c = 0; c < 32; ++c) add_tile(t.near[r][c]);
}

Rect GlScene::centre_4x3(int win_w, int win_h) {
  Rect r;
  r.h = win_h;
  r.w = int(std::lround(win_h * 4.0 / 3.0));
  if (r.w > win_w) {
    r.w = win_w;
    r.h = int(std::lround(win_w * 3.0 / 4.0));
  }
  r.x = (win_w - r.w) / 2;
  r.y = (win_h - r.h) / 2;
  return r;
}

Rect GlScene::wide_area(int win_w, int win_h, float max_widen) {
  const Rect c = centre_4x3(win_w, win_h);
  Rect r = c;
  r.w = std::min(win_w, int(std::lround(double(c.w) * double(max_widen))));
  r.x = (win_w - r.w) / 2;
  return r;
}

void GlScene::begin(int win_w, int win_h) {
  Impl& s = *p_;
  const GL& gl = s.gl;
  s.win_w = win_w;
  s.win_h = win_h;
  gl.Viewport(0, 0, win_w, win_h);
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_DEPTH_TEST);
  gl.Disable(GL_CULL_FACE);
  gl.Disable(GL_STENCIL_TEST);
  gl.Disable(GL_BLEND);
  gl.DepthMask(0);
  gl.ColorMask(1, 1, 1, 1);
  gl.StencilMask(0xFF);
  gl.ClearColor(0, 0, 0, 1);
  gl.ClearStencil(0);
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void GlScene::draw_scene(const Frame& f, const uint32_t* palette, const Rect& area) {
  Impl& s = *p_;
  if (area.w <= 0 || area.h <= 0) return;
  Impl::Target t;
  // GL's window origin is bottom left
  t.x = area.x;
  t.y = s.win_h - area.y - area.h;
  t.w = area.w;
  t.h = area.h;
  t.vw = area.w;
  t.vh = area.h;
  const float half = float(1.0 / (320.0 * std::max(1.0, double(f.widen))));
  const float clip[9] = {-1, 0, 0, 0, 0.9f, 0, -half, 0.105f, 1};
  std::copy(clip, clip + 9, t.clip);
  s.scene(f, palette, t);
}

void GlScene::Impl::eye_projection(const EyeView& e, double out[9]) {
  const double dx = double(e.tan_right) - double(e.tan_left), dy = double(e.tan_up) - double(e.tan_down);
  const double p[9] = {-2.0 / dx, 0, -(double(e.tan_right) + double(e.tan_left)) / dx,
                       0, 2.0 / dy, -(double(e.tan_up) + double(e.tan_down)) / dy,
                       0, 0, 1};
  // P * from_head
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
      double v = 0;
      for (int k = 0; k < 3; ++k) v += p[r * 3 + k] * double(e.from_head[size_t(k * 3 + c)]);
      out[r * 3 + c] = v;
    }
}

void GlScene::draw_scene_eye(const Frame& f, const uint32_t* palette, const EyeView& eye) {
  Impl& s = *p_;
  if (eye.width <= 0 || eye.height <= 0) return;
  // The frame's view space is the head's, scaled: true direction =
  // (x widen / s0, y heighten / s1, z / s2).
  const double k[3] = {double(f.widen) / double(f.camera.scale[0]), double(f.heighten) / double(f.camera.scale[1]),
                       1.0 / double(f.camera.scale[2])};
  double pe[9];
  Impl::eye_projection(eye, pe);
  Impl::Target t;
  t.w = eye.width;
  t.h = eye.height;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) t.clip[c * 3 + r] = float(pe[r * 3 + c] * k[c]);
  // output pixels per unit of x/z and y/z of the frame's view space, at the
  // centre of the eye's picture
  t.vw = 2.0 * double(eye.width) / (double(eye.tan_right) - double(eye.tan_left)) * k[0] / k[2];
  t.vh = 2.0 * double(eye.height) / (double(eye.tan_up) - double(eye.tan_down)) * k[1] / k[2] / 0.9;
  // Stereo: every primitive whose distance is known is seen from the eye's
  // own place.
  for (int i = 0; i < 3; ++i) {
    t.eye[i] = double(eye.pos[size_t(i)]) * double(kUnitsPerMetre);
    t.axis[i] = k[i];
    t.offset = t.offset || eye.pos[size_t(i)] != 0.f;
  }
  s.scene(f, palette, t);
}

void GlScene::Impl::scene(const Frame& f, const uint32_t* palette, const Target& target) {
  Impl& s = *this;
  const GL& gl = s.gl;
  s.stats = {};
  s.buf.clear();
  s.batches.clear();
  const double widen = std::max(1e-3, double(f.widen)), heighten = std::max(1e-3, double(f.heighten));
  // one low-res pixel / row in the clip units of the original's projection
  // (the view is 320 * widen x 200 * heighten)
  const double px = 2.0 / (320.0 * widen), py = 2.0 / (200.0 * heighten);
  const double half = 1.0 / (320.0 * widen);  // the projector's half-pixel offset
  // a low-res position (clip units) as a view-space point at depth 1
  auto clip_point = [&](double cx, double cy) { s.push(-cx - half, (cy - 0.105) / 0.9, 1.0); };
  auto quad = [&](double cx, double cy, double hx, double hy) {
    clip_point(cx - hx, cy - hy);
    clip_point(cx + hx, cy - hy);
    clip_point(cx + hx, cy + hy);
    clip_point(cx - hx, cy - hy);
    clip_point(cx + hx, cy + hy);
    clip_point(cx - hx, cy + hy);
  };
  // Lines are one low-res pixel wide in the original; at the window's
  // resolution a narrower stroke keeps their weight without the steps.
  const double row_px = target.vh / (200.0 * heighten);
  const double stroke = std::max(1.25, 0.6 * row_px) / row_px;  // in rows
  auto line = [&](V3 a, V3 b) {
    const double zmin = 1e-4 * std::max({std::fabs(a.z), std::fabs(b.z), 1e-6});
    if (a.z < zmin && b.z < zmin) return;
    if (a.z < zmin) std::swap(a, b);
    if (b.z < zmin) {
      const double t = (zmin - a.z) / (b.z - a.z);
      b = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), zmin};
    }
    const double ax = -a.x / a.z - half, ay = 0.105 + 0.9 * a.y / a.z;
    const double bx = -b.x / b.z - half, by = 0.105 + 0.9 * b.y / b.z;
    // direction in window pixels, so the stroke is equally wide everywhere
    double dx = (bx - ax) * target.vw, dy = (by - ay) * target.vh;
    const double len = std::hypot(dx, dy);
    if (len < 1e-6) {
      dx = 1;
      dy = 0;
    } else {
      dx /= len;
      dy /= len;
    }
    const double w = 0.5 * stroke * row_px;  // half width in window pixels
    const double ex = dx * w * 2 / target.vw, ey = dy * w * 2 / target.vh;      // along
    const double nx = -dy * w * 2 / target.vw, ny = dx * w * 2 / target.vh;     // across
    const double q[4][2] = {{ax - ex - nx, ay - ey - ny}, {ax - ex + nx, ay - ey + ny},
                            {bx + ex + nx, by + ey + ny}, {bx + ex - nx, by + ey - ny}};
    for (int i : {0, 1, 2, 0, 2, 3}) clip_point(q[i][0], q[i][1]);
  };

  std::vector<V3> v;
  std::vector<std::pair<double, double>> flat;
  std::vector<int> tris;
  for (const Prim& p : f.prims) {
    const size_t start = s.buf.size();
    v.clear();
    for (int i = 0; i < p.count; ++i) v.push_back(xform(f.refine, f.verts[p.first + size_t(i)]));
    if (target.offset && p.unit > 0) {
      // the eye in the primitive's own units, in the frame's view space
      const double u = double(p.unit);
      const V3 e{target.eye[0] / u / target.axis[0], target.eye[1] / u / target.axis[1],
                 target.eye[2] / u / target.axis[2]};
      for (V3& q : v) q = {q.x - e.x, q.y - e.y, q.z - e.z};
    }
    switch (p.kind) {
      case Prim::Poly: {
        // triangulate in the polygon's plane (it may be concave and may
        // pass behind the eye)
        V3 n{0, 0, 0};
        for (size_t i = 0; i < v.size(); ++i) {
          const V3 &a = v[i], &b = v[(i + 1) % v.size()];
          n.x += (a.y - b.y) * (a.z + b.z);
          n.y += (a.z - b.z) * (a.x + b.x);
          n.z += (a.x - b.x) * (a.y + b.y);
        }
        const double nx = std::fabs(n.x), ny = std::fabs(n.y), nz = std::fabs(n.z);
        flat.clear();
        for (const V3& q : v) {
          if (nx >= ny && nx >= nz) flat.push_back({q.y, q.z});
          else if (ny >= nz) flat.push_back({q.x, q.z});
          else flat.push_back({q.x, q.y});
        }
        tris.clear();
        ear_clip(flat, tris);
        for (int i : tris) s.push(v[size_t(i)].x, v[size_t(i)].y, v[size_t(i)].z);
        s.stats.triangles += int(tris.size() / 3);
        // The original's fill never loses a polygon: one or two pixels
        // wide or high it becomes a line or a dot ($44dfe).
        bool front = true;
        double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
        size_t lo = 0, hi = 0;
        for (size_t i = 0; i < v.size() && front; ++i) {
          if (v[i].z <= 0) { front = false; break; }
          const double cx = -v[i].x / v[i].z - half, cy = 0.105 + 0.9 * v[i].y / v[i].z;
          if (cx < x0) { x0 = cx; lo = i; }
          if (cx > x1) { x1 = cx; hi = i; }
          y0 = std::min(y0, cy);
          y1 = std::max(y1, cy);
        }
        if (front) {
          const double dx = (x1 - x0) / px, dy = (y1 - y0) / py;
          if (dx <= 1.0 && dy <= 1.0) {
            quad((x0 + x1) / 2, (y0 + y1) / 2, 0.5 * px * std::max(dx, 1.0), 0.5 * py * std::max(dy, 1.0));
          } else if (dx <= 1.0 || dy <= 1.0) {
            if (dy > dx) {  // tall: from the lowest to the highest vertex
              lo = hi = 0;
              for (size_t i = 1; i < v.size(); ++i) {
                if (v[i].y / v[i].z < v[lo].y / v[lo].z) lo = i;
                if (v[i].y / v[i].z > v[hi].y / v[hi].z) hi = i;
              }
            }
            line(v[lo], v[hi]);
          }
        }
        break;
      }
      case Prim::Line:
        if (v.size() == 2) line(v[0], v[1]);
        break;
      case Prim::Point:
      case Prim::Disc: {
        if (v.empty() || v[0].z <= 0) break;
        // pixel centre: column 159 - 160 x/z, row 90 - 90 y/z (one row
        // below a polygon vertex)
        double cx = -v[0].x / v[0].z - half, cy = 0.105 + 0.9 * v[0].y / v[0].z - py;
        if (p.kind == Prim::Disc) {
          const int seg = 32;
          const double r = double(p.radius);
          for (int i = 0; i < seg; ++i) {
            const double a0 = 2 * M_PI * i / seg, a1 = 2 * M_PI * (i + 1) / seg;
            clip_point(cx, cy);
            clip_point(cx + r * px * std::cos(a0), cy + r * py * std::sin(a0));
            clip_point(cx + r * px * std::cos(a1), cy + r * py * std::sin(a1));
          }
        } else {
          if (p.w == 2) cx -= 0.5 * px;  // columns x-1 and x
          if (p.h == 2) cy += 0.5 * py;
          quad(cx, cy, 0.5 * px * p.w, 0.5 * py * p.h);
        }
        break;
      }
    }
    s.batch(p.op, start);
    ++s.stats.prims;
  }
  const size_t prim_floats = s.buf.size();

  // ---- ground: water polygons at sea level, relative to the camera ----
  const size_t water_first = s.buf.size() / 3;
  double eye_world[3] = {0, 0, 0};  // the eye relative to the camera, world axes
  if (target.offset)
    for (int c = 0; c < 3; ++c)
      for (int r = 0; r < 3; ++r) eye_world[c] += double(f.camera.rot[size_t(r * 3 + c)]) * target.eye[r];
  for (const Impl::WaterTri& t : s.water) {
    if (t.min_alt > f.ground_altitude) continue;
    for (int i = 0; i < 3; ++i)
      s.push(t.x[i] - f.camera.x - eye_world[0], -f.camera.y - eye_world[1], t.z[i] - f.camera.z - eye_world[2]);
    ++s.stats.water_triangles;
  }
  const size_t water_count = s.buf.size() / 3 - water_first;
  // sky: every direction above the horizontal, as points at infinity
  const size_t sky_first = s.buf.size() / 3;
  const double h[5][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 0}};
  for (int i = 0; i < 4; ++i) {
    s.push(0, 1, 0);
    s.push(h[i][0], 0, h[i][1]);
    s.push(h[i + 1][0], 0, h[i + 1][1]);
  }
  (void)prim_floats;

  // ---- draw ----
  gl.Viewport(target.x, target.y, target.w, target.h);
  gl.Scissor(target.x, target.y, target.w, target.h);
  gl.Enable(GL_SCISSOR_TEST);
  gl.Enable(GL_STENCIL_TEST);
  gl.StencilMask(0xFF);
  gl.ClearStencil(f.backdrop);
  gl.Clear(GL_STENCIL_BUFFER_BIT);
  gl.ColorMask(0, 0, 0, 0);
  gl.UseProgram(s.scene_prog);
  gl.BindBuffer(GL_ARRAY_BUFFER, s.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(s.buf.size() * sizeof(float)), s.buf.data(),
                GL_STREAM_DRAW);
  gl.EnableVertexAttribArray(0);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);
  // view space as built above (the original's projection inverted) -> clip
  gl.UniformMatrix3fv(s.u_clip, 1, 0, target.clip);

  // world -> view of the exact camera (row scales hold the field of view)
  float cam[9];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
      float k = f.camera.scale[size_t(r)];
      if (r == 0) k /= float(widen);
      if (r == 1) k /= float(heighten);
      cam[c * 3 + r] = f.camera.rot[size_t(r * 3 + c)] * k;  // column-major for GL
    }
  gl.UniformMatrix3fv(s.u_m, 1, 0, cam);
  gl.Uniform3f(s.u_off, 0, 0, 0);
  {
    Batch w;
    if (f.water_all_planes) {
      w.op.set = uint8_t(f.water_colour & 15);
      w.op.clr = uint8_t(~f.water_colour & 15);
    } else {
      w.op.set = 2;
    }
    w.first = GLint(water_first);
    w.count = GLsizei(water_count);
    if (w.count > 0) s.apply(w);
    Batch sky;  // plane 0 set; over the open sea plane 1 is cleared as well
    sky.op.set = 1;
    if (f.backdrop & 2) sky.op.clr = 2;
    sky.first = GLint(sky_first);
    sky.count = 12;
    s.apply(sky);
  }
  const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  gl.UniformMatrix3fv(s.u_m, 1, 0, ident);
  for (const Batch& b : s.batches) s.apply(b);
  s.stats.batches = int(s.batches.size());

  // ---- palette: one pass per index ----
  gl.ColorMask(1, 1, 1, 1);
  gl.StencilMask(0);
  gl.StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  s.buf.clear();
  const double full[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
  for (auto& q : full) s.push(-q[0], q[1], 1.0);
  gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(s.buf.size() * sizeof(float)), s.buf.data(),
                GL_STREAM_DRAW);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);
  const float flip[9] = {-1, 0, 0, 0, 1, 0, 0, 0, 1};
  gl.UniformMatrix3fv(s.u_clip, 1, 0, flip);
  for (int i = 0; i < 16; ++i) {
    const uint32_t c = palette[i];
    gl.StencilFunc(GL_EQUAL, i, 0x0F);
    gl.Uniform4f(s.u_colour, float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f,
                 float(c & 255) / 255.f, 1.f);
    gl.DrawArrays(GL_TRIANGLES, 0, 6);
  }
  gl.Disable(GL_STENCIL_TEST);
  gl.Disable(GL_SCISSOR_TEST);
  gl.StencilMask(0xFF);
  gl.Viewport(0, 0, s.win_w, s.win_h);
}

void GlScene::Impl::upload(int slot, const uint32_t* argb, int w, int h, bool blend) {
  Impl& s = *this;
  Tex& t = s.tex[slot];
  s.rgba.resize(size_t(w) * size_t(h) * 4);
  for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
    const uint32_t c = argb[i];
    // premultiplied alpha: filtering towards a transparent texel must not darken
    const uint32_t a = blend ? c >> 24 : 255;
    s.rgba[i * 4] = uint8_t(((c >> 16) & 255) * a / 255);
    s.rgba[i * 4 + 1] = uint8_t(((c >> 8) & 255) * a / 255);
    s.rgba[i * 4 + 2] = uint8_t((c & 255) * a / 255);
    s.rgba[i * 4 + 3] = uint8_t(a);
  }
  gl.ActiveTexture(GL_TEXTURE0);
  if (!t.id) gl.GenTextures(1, &t.id);
  gl.BindTexture(GL_TEXTURE_2D, t.id);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (t.w != w || t.h != h) {
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.rgba.data());
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    t.w = w;
    t.h = h;
  } else {
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, s.rgba.data());
  }
}

// quad: 6 vertices of x, y, z, u, v; clip: column-major; sx, sy: output
// pixels per texel (1: plain bilinear filtering).
void GlScene::Impl::image(int slot, const float* quad, const float* clip, float sx, float sy, bool blend) {
  Impl& s = *this;
  const Tex& t = s.tex[slot];
  if (!t.id) return;
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, t.id);
  gl.UseProgram(s.image_prog);
  gl.Uniform1i(s.u_image, 0);
  gl.Uniform4f(s.u_tex, float(t.w), float(t.h), std::max(1.f, sx), std::max(1.f, sy));
  gl.UniformMatrix3fv(s.u_image_clip, 1, 0, clip);
  gl.BindBuffer(GL_ARRAY_BUFFER, s.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(30 * sizeof(float)), quad, GL_STREAM_DRAW);
  gl.EnableVertexAttribArray(0);
  gl.EnableVertexAttribArray(1);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 5 * sizeof(float), nullptr);
  gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, 5 * sizeof(float),
                         reinterpret_cast<const void*>(3 * sizeof(float)));
  if (blend) {
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }
  gl.DrawArrays(GL_TRIANGLES, 0, 6);
  gl.Disable(GL_BLEND);
  gl.DisableVertexAttribArray(1);
}

void GlScene::draw_image(int slot, const uint32_t* argb, int w, int h, const Rect& dst, bool blend,
                         bool smooth) {
  Impl& s = *p_;
  if (slot < 0 || slot >= 4 || w <= 0 || h <= 0 || dst.w <= 0 || dst.h <= 0) return;
  s.upload(slot, argb, w, h, blend);
  const float x0 = 2.f * float(dst.x) / float(s.win_w) - 1.f;
  const float x1 = 2.f * float(dst.x + dst.w) / float(s.win_w) - 1.f;
  const float y0 = 1.f - 2.f * float(dst.y) / float(s.win_h);
  const float y1 = 1.f - 2.f * float(dst.y + dst.h) / float(s.win_h);
  const float q[] = {x0, y0, 1, 0, 0, x1, y0, 1, 1, 0, x1, y1, 1, 1, 1,
                     x0, y0, 1, 0, 0, x1, y1, 1, 1, 1, x0, y1, 1, 0, 1};
  const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  // smooth: plain bilinear; otherwise sharp enlarged pixels
  s.image(slot, q, ident, smooth ? 1.f : float(dst.w) / float(w), smooth ? 1.f : float(dst.h) / float(h),
          blend);
}

void GlScene::set_image(int slot, const uint32_t* argb, int w, int h, bool blend) {
  if (slot < 0 || slot >= 4 || w <= 0 || h <= 0) return;
  p_->upload(slot, argb, w, h, blend);
}

void GlScene::draw_screen_eye(int slot, const EyeView& eye, const std::array<float, 9>& head_from_seat,
                              const std::array<float, 3>& scale, float size, bool blend, bool console,
                              float distance) {
  Impl& s = *p_;
  if (slot < 0 || slot >= 4 || !s.tex[slot].id || eye.width <= 0 || eye.height <= 0) return;
  // The 320x200 screen in the directions the original's projection gives
  // its pixels: columns 0..320 are x/z = 1..-1 and rows 0..200 are
  // (1 - 0.105) / 0.9 .. (-1 - 0.105) / 0.9 of y/z in the scaled view space.
  const float ax = size * scale[2] / scale[0], ay = size * scale[2] / scale[1];
  const float xl = ax, xr = -ax, yt = ay * (1.f - 0.105f) / 0.9f, yb = ay * (-1.f - 0.105f) / 0.9f;
  float q[] = {xl, yt, 1, 0, 0, xr, yt, 1, 1, 0, xr, yb, 1, 1, 1,
               xl, yt, 1, 0, 0, xr, yb, 1, 1, 1, xl, yb, 1, 0, 1};
  double pe[9];
  Impl::eye_projection(eye, pe);
  float clip[9];
  // At infinity the vertices are seat-space directions; at a distance they
  // are placed in head space (seat direction * distance, turned into the
  // head, minus the eye's place) and only the eye's projection remains.
  const bool near = distance > 0;
  auto place = [&](float* v) {
    if (!near) return;
    double h[3];
    for (int r = 0; r < 3; ++r) {
      h[r] = -double(eye.pos[size_t(r)]);
      for (int c = 0; c < 3; ++c) h[r] += double(head_from_seat[size_t(r * 3 + c)]) * double(v[c]) * double(distance);
    }
    for (int r = 0; r < 3; ++r) v[r] = float(h[r]);
  };
  for (int i = 0; i < 6; ++i) place(&q[i * 5]);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
      double v = 0;
      if (near) v = pe[r * 3 + c];
      else
        for (int k = 0; k < 3; ++k) v += pe[r * 3 + k] * double(head_from_seat[size_t(k * 3 + c)]);
      clip[c * 3 + r] = float(v);
    }
  // output pixels per texel where the eye looks straight at the screen
  const float sx = float(eye.width) / (eye.tan_right - eye.tan_left) * 2.f * ax / float(s.tex[slot].w);
  const float sy = float(eye.height) / (eye.tan_up - eye.tan_down) * (yt - yb) / float(s.tex[slot].h);
  s.gl.Viewport(0, 0, eye.width, eye.height);
  if (console) {
    // what the panel stands on: black from its lower edge downwards
    const float below = -60.f * ay;
    float c[] = {xl, yb, 1, xr, yb, 1, xr, below, 1, xl, yb, 1, xr, below, 1, xl, below, 1};
    for (int i = 0; i < 6; ++i) place(&c[i * 3]);
    const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const GL& gl = s.gl;
    gl.UseProgram(s.scene_prog);
    gl.UniformMatrix3fv(s.u_m, 1, 0, ident);
    gl.Uniform3f(s.u_off, 0, 0, 0);
    gl.UniformMatrix3fv(s.u_clip, 1, 0, clip);
    gl.Uniform4f(s.u_colour, 0, 0, 0, 1);
    gl.BindBuffer(GL_ARRAY_BUFFER, s.vbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof c, c, GL_STREAM_DRAW);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);
    gl.DrawArrays(GL_TRIANGLES, 0, 6);
  }
  s.image(slot, q, clip, sx, sy, blend);
}

void GlScene::set_size(int win_w, int win_h) {
  Impl& s = *p_;
  s.win_w = win_w;
  s.win_h = win_h;
  s.gl.Viewport(0, 0, win_w, win_h);
  s.gl.Disable(GL_SCISSOR_TEST);
  s.gl.Disable(GL_STENCIL_TEST);
  s.gl.ColorMask(1, 1, 1, 1);
}

bool GlScene::read_window(std::vector<uint8_t>& rgb, int win_w, int win_h) {
  Impl& s = *p_;
  if (win_w <= 0 || win_h <= 0) return false;
  std::vector<uint8_t> px(size_t(win_w) * size_t(win_h) * 4);
  s.gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
  s.gl.ReadPixels(0, 0, win_w, win_h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  rgb.resize(size_t(win_w) * size_t(win_h) * 3);
  for (int y = 0; y < win_h; ++y)
    for (int x = 0; x < win_w; ++x) {
      const uint8_t* p = &px[(size_t(win_h - 1 - y) * size_t(win_w) + size_t(x)) * 4];
      uint8_t* o = &rgb[(size_t(y) * size_t(win_w) + size_t(x)) * 3];
      o[0] = p[0];
      o[1] = p[1];
      o[2] = p[2];
    }
  return true;
}

GlScene::Stats GlScene::last_stats() const { return p_->stats; }

}  // namespace render
