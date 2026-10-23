// OpenGL renderer of the 3D view.  See gl_renderer.hpp.
//
// Flat-shaded polygons and palette lookups only (GL 2.1 or GLES 2): the
// plane operations of the original are resolved to colours.  Measured over
// the scenarios, 3D objects, lines and points always write all four planes
// (a plain colour); the ground layer uses three partial operations:
//   - invert plane 1 (islands / lakes): land 4 <-> water 6,
//   - set plane 3 (runways): 12 on land, 14 on water,
//   - set planes 0+1: 7 on both.
// So the ground is a land/water map in the stencil buffer (water tiles set
// it, toggling patches invert it), painted once, and each flat overlay gets
// the colour of its operation on the land or the water class underneath.
#include "native/gl_renderer.hpp"

#include "native/runtime.hpp"
#include "native/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace glr {

namespace {

using native::rd16;
using native::rd32;
using native::rd8;

// ---- GL entry points (GL 2.1 / GLES 2 subset, loaded through the host) --------
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLchar = char;
using GLsizeiptr = ptrdiff_t;

constexpr GLenum GL_TRIANGLES = 0x0004, GL_LINES = 0x0001, GL_POINTS = 0x0000;
constexpr GLenum GL_DEPTH_TEST = 0x0B71, GL_STENCIL_TEST = 0x0B90, GL_LEQUAL = 0x0203;
constexpr GLenum GL_ALWAYS = 0x0207, GL_EQUAL = 0x0202, GL_KEEP = 0x1E00;
constexpr GLenum GL_REPLACE = 0x1E01, GL_INVERT = 0x150A;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x0100;
constexpr GLenum GL_STENCIL_BUFFER_BIT = 0x0400;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40, GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_STENCIL_ATTACHMENT = 0x8D20, GL_DEPTH24_STENCIL8 = 0x88F0;
constexpr GLenum GL_DEPTH_COMPONENT16 = 0x81A5, GL_STENCIL_INDEX8 = 0x8D48;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_RGBA8 = 0x8058, GL_RGBA4 = 0x8056;
constexpr GLenum GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892, GL_STREAM_DRAW = 0x88E0;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05, GL_PROGRAM_POINT_SIZE = 0x8642;
constexpr GLenum GL_VERSION = 0x1F02, GL_RENDERER = 0x1F01;

struct GL {
  void (*GenFramebuffers)(GLsizei, GLuint*);
  void (*BindFramebuffer)(GLenum, GLuint);
  void (*GenRenderbuffers)(GLsizei, GLuint*);
  void (*BindRenderbuffer)(GLenum, GLuint);
  void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
  void (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
  GLenum (*CheckFramebufferStatus)(GLenum);
  void (*DeleteRenderbuffers)(GLsizei, const GLuint*);
  void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
  void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
  void (*ClearStencil)(GLint);
  void (*Clear)(GLbitfield);
  void (*Enable)(GLenum);
  void (*Disable)(GLenum);
  void (*DepthFunc)(GLenum);
  void (*DepthMask)(GLboolean);
  void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
  void (*StencilFunc)(GLenum, GLint, GLuint);
  void (*StencilOp)(GLenum, GLenum, GLenum);
  void (*LineWidth)(GLfloat);
  GLuint (*CreateShader)(GLenum);
  void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
  void (*CompileShader)(GLuint);
  void (*GetShaderiv)(GLuint, GLenum, GLint*);
  void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
  GLuint (*CreateProgram)();
  void (*AttachShader)(GLuint, GLuint);
  void (*BindAttribLocation)(GLuint, GLuint, const GLchar*);
  void (*LinkProgram)(GLuint);
  void (*GetProgramiv)(GLuint, GLenum, GLint*);
  void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
  void (*UseProgram)(GLuint);
  GLint (*GetUniformLocation)(GLuint, const GLchar*);
  void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
  void (*UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
  void (*GenBuffers)(GLsizei, GLuint*);
  void (*BindBuffer)(GLenum, GLuint);
  void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
  void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
  void (*EnableVertexAttribArray)(GLuint);
  void (*DrawArrays)(GLenum, GLint, GLsizei);
  void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
  void (*PixelStorei)(GLenum, GLint);
  const unsigned char* (*GetString)(GLenum);
};

GL gl;

template <typename F>
void load(GetProc proc, F& f, const char* name, std::string& missing) {
  f = reinterpret_cast<F>(proc(name));
  if (!f) missing += std::string(missing.empty() ? "" : ", ") + name;
}

bool load_gl(GetProc p, std::string& err) {
  std::string m;
  load(p, gl.GenFramebuffers, "glGenFramebuffers", m);
  load(p, gl.BindFramebuffer, "glBindFramebuffer", m);
  load(p, gl.GenRenderbuffers, "glGenRenderbuffers", m);
  load(p, gl.BindRenderbuffer, "glBindRenderbuffer", m);
  load(p, gl.RenderbufferStorage, "glRenderbufferStorage", m);
  load(p, gl.FramebufferRenderbuffer, "glFramebufferRenderbuffer", m);
  load(p, gl.CheckFramebufferStatus, "glCheckFramebufferStatus", m);
  load(p, gl.DeleteRenderbuffers, "glDeleteRenderbuffers", m);
  load(p, gl.Viewport, "glViewport", m);
  load(p, gl.ClearColor, "glClearColor", m);
  load(p, gl.ClearStencil, "glClearStencil", m);
  load(p, gl.Clear, "glClear", m);
  load(p, gl.Enable, "glEnable", m);
  load(p, gl.Disable, "glDisable", m);
  load(p, gl.DepthFunc, "glDepthFunc", m);
  load(p, gl.DepthMask, "glDepthMask", m);
  load(p, gl.ColorMask, "glColorMask", m);
  load(p, gl.StencilFunc, "glStencilFunc", m);
  load(p, gl.StencilOp, "glStencilOp", m);
  load(p, gl.LineWidth, "glLineWidth", m);
  load(p, gl.CreateShader, "glCreateShader", m);
  load(p, gl.ShaderSource, "glShaderSource", m);
  load(p, gl.CompileShader, "glCompileShader", m);
  load(p, gl.GetShaderiv, "glGetShaderiv", m);
  load(p, gl.GetShaderInfoLog, "glGetShaderInfoLog", m);
  load(p, gl.CreateProgram, "glCreateProgram", m);
  load(p, gl.AttachShader, "glAttachShader", m);
  load(p, gl.BindAttribLocation, "glBindAttribLocation", m);
  load(p, gl.LinkProgram, "glLinkProgram", m);
  load(p, gl.GetProgramiv, "glGetProgramiv", m);
  load(p, gl.GetProgramInfoLog, "glGetProgramInfoLog", m);
  load(p, gl.UseProgram, "glUseProgram", m);
  load(p, gl.GetUniformLocation, "glGetUniformLocation", m);
  load(p, gl.Uniform4f, "glUniform4f", m);
  load(p, gl.UniformMatrix3fv, "glUniformMatrix3fv", m);
  load(p, gl.GenBuffers, "glGenBuffers", m);
  load(p, gl.BindBuffer, "glBindBuffer", m);
  load(p, gl.BufferData, "glBufferData", m);
  load(p, gl.VertexAttribPointer, "glVertexAttribPointer", m);
  load(p, gl.EnableVertexAttribArray, "glEnableVertexAttribArray", m);
  load(p, gl.DrawArrays, "glDrawArrays", m);
  load(p, gl.ReadPixels, "glReadPixels", m);
  load(p, gl.PixelStorei, "glPixelStorei", m);
  load(p, gl.GetString, "glGetString", m);
  if (!m.empty()) {
    err = "missing GL functions: " + m;
    return false;
  }
  return true;
}

// ---- shaders ----------------------------------------------------------------------
//
// View space is the original's: v = M p, with M the row-scaled view matrix
// $5aed0 / 256 for the ground (world-relative p) and the identity for the
// captured primitives (already in view space); the screen mapping is the
// original projector's (low-res sx = 159.5 - 160 vx/vz, sy = 89.5 -
// 90 vy/vz).  u_param: rows, point size, depth mode (0: linear with the
// near plane at vz = 64, for the ground; 1: logarithmic in vz, computed
// per vertex, for the objects), depth bias.
const char* kVertex = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec3 a_pos;
uniform mat3 u_view;
uniform vec4 u_param;
void main() {
  vec3 v = u_view * a_pos;
  float rows = u_param.x;
  float zlin = v.z - 128.0;
  float zlog = (log2(max(v.z, 1.0e-6)) * (2.0 / 40.0) - 1.0 - u_param.w) * v.z;
  gl_Position = vec4(-0.003125 * v.z - v.x,
                     (1.0 - 179.0 / rows) * v.z + (180.0 / rows) * v.y,
                     mix(zlin, zlog, u_param.z), v.z);
  gl_PointSize = u_param.y;
}
)";

const char* kFragment = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform vec4 u_colour;
void main() { gl_FragColor = u_colour; }
)";

GLuint compile(GLenum type, const char* src, std::string& err) {
  GLuint s = gl.CreateShader(type);
  gl.ShaderSource(s, 1, &src, nullptr);
  gl.CompileShader(s);
  GLint ok = 0;
  gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048] = {};
    gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
    err = std::string("shader: ") + log;
    return 0;
  }
  return s;
}

// ---- captured frame ---------------------------------------------------------------

struct Prim {
  GLenum mode;
  GLint first;
  GLsizei count;
  Ops ops;
  float size = 1;     // points: size in low-res pixels
  float bias = 0;     // depth bias (later primitives of an object win)
  bool flat = false;
};

struct State {
  bool ready = false, enabled = true;
  std::function<void()> make_current;
  GLuint prog = 0, fbo = 0, colour_rb = 0, depth_rb = 0, stencil_rb = 0, vbo = 0;
  int fb_w = 0, fb_h = 0;
  GLint u_view = -1, u_param = -1, u_colour = -1;
  // current frame
  bool capturing = false;
  int object = -1;          // index of the current object, -1 = none yet
  bool distant = false, flat = false;
  int prims_in_object = 0;
  std::vector<float> verts; // view space, world units
  std::vector<Prim> prims;
  // terrain: triangulated water, absolute world coordinates
  struct WaterTri { double x[3], z[3]; int32_t min_alt; };
  std::vector<WaterTri> water;
  std::vector<float> ground;  // per frame
  uint64_t frames = 0;
  bool debug = false;
};

State st;

// ---- triangulation (ear clipping) of the terrain polygons ---------------------------

double cross(double ax, double az, double bx, double bz, double cx, double cz) {
  return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
}

void triangulate(const std::vector<std::pair<double, double>>& in, int32_t min_alt,
                 std::vector<State::WaterTri>& out) {
  std::vector<std::pair<double, double>> v = in;
  v.erase(std::unique(v.begin(), v.end()), v.end());
  if (v.size() > 1 && v.front() == v.back()) v.pop_back();
  if (v.size() < 3) return;
  double area = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    auto& a = v[i];
    auto& b = v[(i + 1) % v.size()];
    area += a.first * b.second - b.first * a.second;
  }
  if (area < 0) std::reverse(v.begin(), v.end());
  std::vector<size_t> idx(v.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
  for (size_t guard = 0; idx.size() > 3 && guard < 10000; ++guard) {
    bool clipped = false;
    for (size_t i = 0; i < idx.size(); ++i) {
      size_t ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
      auto& a = v[ia];
      auto& b = v[ib];
      auto& c = v[ic];
      if (cross(a.first, a.second, b.first, b.second, c.first, c.second) <= 0) continue;
      bool inside = false;
      for (size_t k : idx) {
        if (k == ia || k == ib || k == ic) continue;
        auto& p = v[k];
        if (cross(a.first, a.second, b.first, b.second, p.first, p.second) >= 0 &&
            cross(b.first, b.second, c.first, c.second, p.first, p.second) >= 0 &&
            cross(c.first, c.second, a.first, a.second, p.first, p.second) >= 0) {
          inside = true;
          break;
        }
      }
      if (inside) continue;
      out.push_back({{a.first, b.first, c.first}, {a.second, b.second, c.second}, min_alt});
      idx.erase(idx.begin() + long(i));
      clipped = true;
      break;
    }
    if (!clipped) break;  // degenerate: fan the rest
  }
  for (size_t i = 1; i + 1 < idx.size(); ++i) {
    auto& a = v[idx[0]];
    auto& b = v[idx[i]];
    auto& c = v[idx[i + 1]];
    out.push_back({{a.first, b.first, c.first}, {a.second, b.second, c.second}, min_alt});
  }
}

void build_terrain() {
  terrain::Terrain t;
  terrain::decode([](uint32_t a) { return rd8(a); }, t);
  auto add_tile = [&](const terrain::Tile& tile) {
    for (const terrain::Polygon& p : tile.polys) {
      std::vector<std::pair<double, double>> v;
      for (auto& [x, z] : p.v) v.push_back({double(x), double(z)});
      triangulate(v, p.min_altitude, st.water);
    }
  };
  // Inside the map (far tiles 4-5) the near grid is the detailed version.
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c)
      if (!(r >= 4 && r <= 5 && c >= 4 && c <= 5)) add_tile(t.far[r][c]);
  for (int r = 0; r < 32; ++r)
    for (int c = 0; c < 32; ++c) add_tile(t.near[r][c]);
}

// ---- capture --------------------------------------------------------------------------

// World units per view unit of the current object's vertex stage.
double stage_unit() {
  const uint32_t fp = native::cpu().a[6];
  if (st.distant) return std::ldexp(1.0, 8 + int16_t(rd16(fp - 8)));
  return std::ldexp(1.0, int16_t(rd16(0x5adb0)) + 8 - int16_t(rd16(fp - 6)));
}

bool capture_ok() { return st.ready && st.enabled && st.capturing && st.object >= 0; }

Prim& add_prim(GLenum mode, const Ops& o) {
  Prim p{};
  p.mode = mode;
  p.first = GLint(st.verts.size() / 3);
  p.ops = o;
  p.flat = st.flat;
  p.bias = float(st.prims_in_object++) * 2e-6f;
  st.prims.push_back(p);
  return st.prims.back();
}

void push(double x, double y, double z) {
  st.verts.push_back(float(x));
  st.verts.push_back(float(y));
  st.verts.push_back(float(z));
}

// ---- drawing ------------------------------------------------------------------------

// Colour index of a plane operation on a background index.
uint8_t resolve(const Ops& o, uint8_t bg) { return uint8_t((((bg & ~o.clr) | o.set) ^ o.xr) & 15); }
constexpr uint8_t kLand = 4, kWater = 6;  // backdrop and plane 1 set

void set_colour(uint8_t index) { gl.Uniform4f(st.u_colour, float(index) / 255.f, 0.f, 0.f, 1.f); }
void set_param(float rows, float point, float log_depth, float bias) {
  gl.Uniform4f(st.u_param, rows, point, log_depth, bias);
}
void set_view(const float view[9]) {
  float vt[9];  // GLSL matrices are column-major
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) vt[c * 3 + r] = view[r * 3 + c];
  gl.UniformMatrix3fv(st.u_view, 1, 0, vt);
}
void upload(const std::vector<float>& v) {
  gl.BindBuffer(GL_ARRAY_BUFFER, st.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);
  gl.EnableVertexAttribArray(0);
}
void stencil_class(int water) {
  gl.StencilFunc(GL_EQUAL, water, 1);
  gl.StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

bool ensure_fbo(int w, int h) {
  if (st.fbo && st.fb_w == w && st.fb_h == h) return true;
  if (!st.fbo) gl.GenFramebuffers(1, &st.fbo);
  gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
  GLuint old[3] = {st.colour_rb, st.depth_rb, st.stencil_rb};
  for (GLuint r : old)
    if (r) gl.DeleteRenderbuffers(1, &r);
  st.colour_rb = st.depth_rb = st.stencil_rb = 0;
  auto rb = [&](GLenum format) {
    GLuint r = 0;
    gl.GenRenderbuffers(1, &r);
    gl.BindRenderbuffer(GL_RENDERBUFFER, r);
    gl.RenderbufferStorage(GL_RENDERBUFFER, format, w, h);
    return r;
  };
  // Prefer RGBA8; fall back to RGBA4 on strict GLES2 without the sized formats.
  st.colour_rb = rb(GL_RGBA8);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, st.colour_rb);
  if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    gl.DeleteRenderbuffers(1, &st.colour_rb);
    st.colour_rb = rb(GL_RGBA4);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, st.colour_rb);
  }
  // packed depth/stencil (GL 3, EXT/OES_packed_depth_stencil), else separate
  st.depth_rb = rb(GL_DEPTH24_STENCIL8);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, st.depth_rb);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, st.depth_rb);
  if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    gl.DeleteRenderbuffers(1, &st.depth_rb);
    st.depth_rb = rb(GL_DEPTH_COMPONENT16);
    st.stencil_rb = rb(GL_STENCIL_INDEX8);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, st.depth_rb);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, st.stencil_rb);
  }
  if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::fprintf(stderr, "gl: framebuffer incomplete\n");
    return false;
  }
  st.fb_w = w;
  st.fb_h = h;
  return true;
}

}  // namespace

// ---- public -------------------------------------------------------------------------

bool init(GetProc proc, std::function<void()> make_current, std::string& err) {
  st.make_current = std::move(make_current);
  st.make_current();
  if (!load_gl(proc, err)) return false;
  GLuint vs = compile(GL_VERTEX_SHADER, kVertex, err);
  if (!vs) return false;
  GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment, err);
  if (!fs) return false;
  st.prog = gl.CreateProgram();
  gl.AttachShader(st.prog, vs);
  gl.AttachShader(st.prog, fs);
  gl.BindAttribLocation(st.prog, 0, "a_pos");
  gl.LinkProgram(st.prog);
  GLint ok = 0;
  gl.GetProgramiv(st.prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048] = {};
    gl.GetProgramInfoLog(st.prog, sizeof(log), nullptr, log);
    err = std::string("link: ") + log;
    return false;
  }
  st.u_view = gl.GetUniformLocation(st.prog, "u_view");
  st.u_param = gl.GetUniformLocation(st.prog, "u_param");
  st.u_colour = gl.GetUniformLocation(st.prog, "u_colour");
  gl.GenBuffers(1, &st.vbo);
  build_terrain();
  st.debug = std::getenv("FA18_GL_DEBUG") != nullptr;
  std::fprintf(stderr, "gl: %s / %s, %zu water triangles\n",
               reinterpret_cast<const char*>(gl.GetString(GL_VERSION)),
               reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)), st.water.size());
  st.ready = true;
  return true;
}

bool ready() { return st.ready; }
bool active() { return st.ready && st.enabled; }
void set_enabled(bool on) { st.enabled = on; }

void scene_begin() {
  if (!active()) return;
  st.capturing = true;
  st.object = -1;
  st.verts.clear();
  st.prims.clear();
}

void begin_object(uint32_t mesh, uint32_t stream, bool distant) {
  (void)stream;
  if (!active() || !st.capturing) return;
  ++st.object;
  st.prims_in_object = 0;
  st.distant = distant;
  // flat models (header +7 bit 1, not articulated) and the distant ground
  // objects lie on the ground: painter's order on the ground layer
  const uint8_t flags = rd8(mesh + 7);
  st.flat = distant || ((flags & 2) && !(flags & 1));
}

// Captured primitives are clipped to vz >= kNear here, so that the
// logarithmic depth of the vertex shader only sees points in front.
constexpr double kNear = 64;
struct P3 { double x, y, z; };
void clip_near(std::vector<P3>& v, bool closed) {
  std::vector<P3> out;
  const size_t n = v.size(), edges = closed ? n : n - 1;
  for (size_t i = 0; i < edges; ++i) {
    const P3 &a = v[i], &b = v[(i + 1) % n];
    if (a.z >= kNear) out.push_back(a);
    if ((a.z >= kNear) != (b.z >= kNear)) {
      double t = (kNear - a.z) / (b.z - a.z);
      out.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), kNear});
    }
  }
  if (!closed && v.back().z >= kNear) out.push_back(v.back());
  v.swap(out);
}

void note_poly(const int16_t (*v)[3], int n, int shift, const Ops& o) {
  if (!capture_ok() || o.copy || n < 3) return;
  const double k = stage_unit() * std::ldexp(1.0, shift);
  std::vector<P3> pts;
  for (int i = 0; i < n; ++i) pts.push_back({v[i][0] * k, v[i][1] * k, v[i][2] * k});
  clip_near(pts, true);
  if (pts.size() < 3) return;
  Prim& p = add_prim(GL_TRIANGLES, o);
  for (size_t i = 1; i + 1 < pts.size(); ++i)
    for (size_t j : {size_t(0), i, i + 1}) push(pts[j].x, pts[j].y, pts[j].z);
  p.count = GLsizei(st.verts.size() / 3) - p.first;
}

void note_line(const double a[3], const double b[3], const Ops& o) {
  if (!capture_ok() || o.copy) return;
  const double k = stage_unit();
  std::vector<P3> pts = {{a[0] * k, a[1] * k, a[2] * k}, {b[0] * k, b[1] * k, b[2] * k}};
  clip_near(pts, false);
  if (pts.size() != 2) return;
  Prim& p = add_prim(GL_LINES, o);
  push(pts[0].x, pts[0].y, pts[0].z);
  push(pts[1].x, pts[1].y, pts[1].z);
  p.count = 2;
}

void note_point(const double q[3], int kind, int radius, const Ops& o) {
  if (!capture_ok() || o.copy) return;
  const double k = stage_unit();
  const double x = q[0] * k, z = q[2] * k;
  if (z < kNear) return;
  const double y = q[1] * k - z / 90.0;  // the original plots one row lower
  if (kind == 3) {  // disc: a polygon around the point in the view plane
    Prim& p = add_prim(GL_TRIANGLES, o);
    const int seg = 24;
    for (int i = 0; i < seg; ++i) {
      double a0 = 2 * M_PI * i / seg, a1 = 2 * M_PI * (i + 1) / seg;
      push(x, y, z);
      push(x + radius * z / 160.0 * std::cos(a0), y + radius * z / 90.0 * std::sin(a0), z);
      push(x + radius * z / 160.0 * std::cos(a1), y + radius * z / 90.0 * std::sin(a1), z);
    }
    p.count = GLsizei(st.verts.size() / 3) - p.first;
    return;
  }
  Prim& p = add_prim(GL_POINTS, o);
  p.size = kind == 0 ? 1.f : 2.f;
  push(x, y, z);
  p.count = 1;
}

bool render_layer(std::vector<uint8_t>& px, int w, int h, int S) {
  if (!active() || !st.capturing) return false;
  st.capturing = false;
  st.make_current();
  if (!ensure_fbo(w, h)) return false;
  const float rows = float(h / S);
  gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
  gl.Viewport(0, 0, w, h);
  gl.UseProgram(st.prog);
#ifndef FA18_OPENGLES2
  gl.Enable(GL_PROGRAM_POINT_SIZE);  // desktop GL; GLES always uses gl_PointSize
#endif
  gl.ColorMask(1, 1, 1, 1);
  gl.DepthMask(1);
  gl.ClearColor(5.f / 255.f, 0, 0, 1);  // sky
  gl.ClearStencil(0);
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  gl.LineWidth(float(std::max(1, S)));
  gl.Disable(GL_DEPTH_TEST);
  gl.DepthMask(0);

  // ---- ground: land plane and water in world space ----
  float view[9];
  for (int i = 0; i < 9; ++i) view[i] = float(int16_t(rd16(0x5aed0 + 2u * uint32_t(i)))) / 256.f;
  // camera = cell origin - $5ad5a (the frame of the object positions)
  const double cam_x = double(int32_t(rd16(0x5ac44)) << 22) - double(int32_t(rd32(0x5ad5a)));
  const double cam_z = double(int32_t(rd16(0x5ac46)) << 22) - double(int32_t(rd32(0x5ad62)));
  const double ground_y = double(int32_t(rd32(0x5ad5e)));
  const int32_t alt = terrain::scaled_altitude(int32_t(rd32(0x5ad70)), rd8(0x5aad5),
                                               int16_t(rd16(0x5ad3a)));
  std::vector<float>& g = st.ground;
  g.clear();
  const double R = std::ldexp(1.0, 31);
  const double quad[6][2] = {{-R, -R}, {R, -R}, {R, R}, {-R, -R}, {R, R}, {-R, R}};
  for (auto& q : quad) {
    g.push_back(float(q[0]));
    g.push_back(float(ground_y));
    g.push_back(float(q[1]));
  }
  for (const State::WaterTri& t : st.water) {
    if (t.min_alt > alt && !native::mach().full_detail) continue;
    for (int i = 0; i < 3; ++i) {
      g.push_back(float(t.x[i] - cam_x));
      g.push_back(float(ground_y));
      g.push_back(float(t.z[i] - cam_z));
    }
  }
  upload(g);
  set_view(view);
  set_param(rows, 1, 0, 0);
  // stencil bit 0 = water: the water tiles set it, flat patches whose
  // operation turns land into water and back (plane 1 inverted) toggle it
  gl.Enable(GL_STENCIL_TEST);
  gl.ColorMask(0, 0, 0, 0);
  gl.StencilFunc(GL_ALWAYS, 1, 1);
  gl.StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
  gl.DrawArrays(GL_TRIANGLES, 6, GLsizei(g.size() / 3) - 6);
  const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  auto toggles = [](const Prim& p) {
    return p.flat && p.mode == GL_TRIANGLES && resolve(p.ops, kLand) == kWater &&
           resolve(p.ops, kWater) == kLand;
  };
  bool any_toggle = false;
  for (const Prim& p : st.prims) any_toggle |= toggles(p);
  if (any_toggle) {
    upload(st.verts);
    set_view(ident);
    gl.StencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    for (const Prim& p : st.prims)
      if (toggles(p)) gl.DrawArrays(p.mode, p.first, p.count);
    upload(g);
    set_view(view);
  }
  gl.ColorMask(1, 0, 0, 0);
  stencil_class(0);
  set_colour(kLand);
  gl.DrawArrays(GL_TRIANGLES, 0, 6);
  stencil_class(1);
  set_colour(rd8(0x5aaa8) ? rd16(0x5ac4c) & 15 : kWater);
  gl.DrawArrays(GL_TRIANGLES, 0, 6);

  // ---- flat objects: their colour on land / water ----
  upload(st.verts);
  set_view(ident);
  auto point_size = [&](const Prim& p) { return p.mode == GL_POINTS ? p.size * float(S) : 1.f; };
  for (const Prim& p : st.prims) {
    if (!p.flat || toggles(p)) continue;
    set_param(rows, point_size(p), 0, 0);
    const uint8_t on_land = resolve(p.ops, kLand), on_water = resolve(p.ops, kWater);
    if (on_land == on_water) {
      gl.Disable(GL_STENCIL_TEST);
      set_colour(on_land);
      gl.DrawArrays(p.mode, p.first, p.count);
      gl.Enable(GL_STENCIL_TEST);
      continue;
    }
    stencil_class(0);
    set_colour(on_land);
    gl.DrawArrays(p.mode, p.first, p.count);
    stencil_class(1);
    set_colour(on_water);
    gl.DrawArrays(p.mode, p.first, p.count);
  }
  gl.Disable(GL_STENCIL_TEST);

  // ---- 3D objects: flat colours with the depth test ----
  gl.Enable(GL_DEPTH_TEST);
  gl.DepthFunc(GL_LEQUAL);
  gl.DepthMask(1);
  for (const Prim& p : st.prims) {
    if (p.flat) continue;
    set_param(rows, point_size(p), 1, p.bias);
    set_colour(resolve(p.ops, kLand));
    gl.DrawArrays(p.mode, p.first, p.count);
  }
  gl.Disable(GL_DEPTH_TEST);

  // ---- read back the palette indices ----
  static std::vector<uint8_t> rgba;
  rgba.resize(size_t(w) * size_t(h) * 4);
  gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
  gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  px.resize(size_t(w) * size_t(h));
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = &rgba[size_t(h - 1 - y) * size_t(w) * 4];
    uint8_t* dst = &px[size_t(y) * size_t(w)];
    for (int x = 0; x < w; ++x) dst[x] = src[x * 4] & 15;
  }
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  if (st.debug && (++st.frames % 100) == 0)
    std::fprintf(stderr, "gl: frame %llu objects %d prims %zu\n", (unsigned long long)st.frames,
                 st.object + 1, st.prims.size());
  return true;
}

}  // namespace glr
