// OpenGL renderer of the 3D view.  See gl_renderer.hpp.
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
constexpr GLenum GL_DEPTH_TEST = 0x0B71, GL_COLOR_LOGIC_OP = 0x0BF2, GL_LEQUAL = 0x0203;
constexpr GLenum GL_AND = 0x1501, GL_OR = 0x1507, GL_XOR = 0x1506, GL_COPY = 0x1503;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x0100;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40, GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_RGBA8 = 0x8058, GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892, GL_STATIC_DRAW = 0x88E4, GL_STREAM_DRAW = 0x88E0;
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
  void (*ClearDepth)(double);
  void (*Clear)(GLbitfield);
  void (*Enable)(GLenum);
  void (*Disable)(GLenum);
  void (*DepthFunc)(GLenum);
  void (*DepthMask)(GLboolean);
  void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
  void (*LogicOp)(GLenum);
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
  void (*Uniform1f)(GLint, GLfloat);
  void (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
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
bool load(GetProc proc, F& f, const char* name, std::string& missing) {
  f = reinterpret_cast<F>(proc(name));
  if (!f) missing += std::string(missing.empty() ? "" : ", ") + name;
  return f != nullptr;
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
  load(p, gl.ClearDepth, "glClearDepth", m);
  load(p, gl.Clear, "glClear", m);
  load(p, gl.Enable, "glEnable", m);
  load(p, gl.Disable, "glDisable", m);
  load(p, gl.DepthFunc, "glDepthFunc", m);
  load(p, gl.DepthMask, "glDepthMask", m);
  load(p, gl.ColorMask, "glColorMask", m);
  load(p, gl.LogicOp, "glLogicOp", m);
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
  load(p, gl.Uniform1f, "glUniform1f", m);
  load(p, gl.Uniform3f, "glUniform3f", m);
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
// View space is the original's: v = M (R (a_pos * scale) + t), with M the
// row-scaled view matrix $5aed0 / 256; the screen mapping is the one of the
// original projector (low-res sx = 159.5 - 160 vx/vz, sy = 89.5 - 90 vy/vz).
// Depth is logarithmic in vz (the world spans 2^29 units).
const char* kVertex = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec3 a_pos;
uniform mat3 u_view;
uniform mat3 u_obj;
uniform vec3 u_t;
uniform float u_scale;
uniform vec4 u_proj;   // rows, point size, near, unused
varying float v_z;
void main() {
  vec3 v = u_view * (u_obj * (a_pos * u_scale) + u_t);
  v_z = v.z;
  float rows = u_proj.x;
  gl_Position = vec4(-0.003125 * v.z - v.x,
                     (1.0 - 179.0 / rows) * v.z + (180.0 / rows) * v.y,
                     v.z - 2.0 * u_proj.z, v.z);
  gl_PointSize = u_proj.y;
}
)";

const char* kFragment = R"(
#ifdef GL_ES
#extension GL_EXT_frag_depth : enable
precision highp float;
#endif
uniform vec4 u_colour;
uniform vec2 u_depth;  // 1 / log2(far), bias
varying float v_z;
void main() {
  gl_FragColor = u_colour;
#ifdef GL_ES
  gl_FragDepthEXT = clamp(log2(max(v_z, 1.0)) * u_depth.x - u_depth.y, 0.0, 1.0);
#else
  gl_FragDepth = clamp(log2(max(v_z, 1.0)) * u_depth.x - u_depth.y, 0.0, 1.0);
#endif
}
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
  float size = 1;     // points: size in hi-res pixels
  float bias = 0;     // depth bias (later primitives of an object win)
  bool flat = false;
};

struct State {
  bool ready = false;
  std::function<void()> make_current;
  GLuint prog = 0, fbo = 0, colour_rb = 0, depth_rb = 0, vbo = 0;
  int fb_w = 0, fb_h = 0;
  GLint u_view = -1, u_obj = -1, u_t = -1, u_scale = -1, u_proj = -1, u_colour = -1, u_depth = -1;
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

bool capture_ok() { return st.ready && st.capturing && st.object >= 0; }

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

void set_colour(uint8_t index) { gl.Uniform4f(st.u_colour, float(index) / 255.f, 0.f, 0.f, 1.f); }

// Draw with the original's plane operation: clear `clr`, set `set`,
// invert `xr` (bit b = display plane b = palette index bit b).
void draw_ops(GLenum mode, GLint first, GLsizei count, const Ops& o, bool depth) {
  if (((o.set | o.clr) & 15) == 15 && !o.xr) {
    set_colour(o.set & 15);
    gl.DrawArrays(mode, first, count);
    return;
  }
  gl.Enable(GL_COLOR_LOGIC_OP);
  bool first_pass = true;
  auto pass = [&](GLenum op, uint8_t value) {
    gl.LogicOp(op);
    set_colour(value);
    if (depth && !first_pass) gl.DepthMask(0);
    gl.DrawArrays(mode, first, count);
    first_pass = false;
  };
  if (o.clr & 15) pass(GL_AND, uint8_t(~o.clr));
  if (o.set & 15) pass(GL_OR, uint8_t(o.set & 15));
  if (o.xr & 15) pass(GL_XOR, uint8_t(o.xr & 15));
  if (depth) gl.DepthMask(1);
  gl.Disable(GL_COLOR_LOGIC_OP);
}

void set_transform(const float view[9]) {
  // GLSL matrices are column-major: pass the transpose of the row-major view.
  float vt[9];
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) vt[c * 3 + r] = view[r * 3 + c];
  const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  gl.UniformMatrix3fv(st.u_view, 1, 0, vt);
  gl.UniformMatrix3fv(st.u_obj, 1, 0, ident);
  gl.Uniform3f(st.u_t, 0, 0, 0);
  gl.Uniform1f(st.u_scale, 1.f);
}

bool ensure_fbo(int w, int h) {
  if (st.fbo && st.fb_w == w && st.fb_h == h) return true;
  if (!st.fbo) gl.GenFramebuffers(1, &st.fbo);
  gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
  if (st.colour_rb) {
    gl.DeleteRenderbuffers(1, &st.colour_rb);
    gl.DeleteRenderbuffers(1, &st.depth_rb);
  }
  gl.GenRenderbuffers(1, &st.colour_rb);
  gl.BindRenderbuffer(GL_RENDERBUFFER, st.colour_rb);
  gl.RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, st.colour_rb);
  gl.GenRenderbuffers(1, &st.depth_rb);
  gl.BindRenderbuffer(GL_RENDERBUFFER, st.depth_rb);
  gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, st.depth_rb);
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
  st.u_obj = gl.GetUniformLocation(st.prog, "u_obj");
  st.u_t = gl.GetUniformLocation(st.prog, "u_t");
  st.u_scale = gl.GetUniformLocation(st.prog, "u_scale");
  st.u_proj = gl.GetUniformLocation(st.prog, "u_proj");
  st.u_colour = gl.GetUniformLocation(st.prog, "u_colour");
  st.u_depth = gl.GetUniformLocation(st.prog, "u_depth");
  gl.GenBuffers(1, &st.vbo);
  build_terrain();
  st.debug = std::getenv("FA18_GL_DEBUG") != nullptr;
  std::fprintf(stderr, "gl: %s / %s, %zu water triangles\n",
               reinterpret_cast<const char*>(gl.GetString(GL_VERSION)),
               reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)), st.water.size());
  st.ready = true;
  return true;
}

bool active() { return st.ready; }

void scene_begin() {
  if (!st.ready) return;
  st.capturing = true;
  st.object = -1;
  st.verts.clear();
  st.prims.clear();
}

void begin_object(uint32_t mesh, uint32_t stream, bool distant) {
  (void)stream;
  if (!st.ready || !st.capturing) return;
  ++st.object;
  st.prims_in_object = 0;
  st.distant = distant;
  // flat models (header +7 bit 1, not articulated) and the distant ground
  // objects lie on the ground: painter's order on the ground layer
  const uint8_t flags = rd8(mesh + 7);
  st.flat = distant || ((flags & 2) && !(flags & 1));
}

void note_poly(const int16_t (*v)[3], int n, int shift, const Ops& o) {
  if (!capture_ok() || o.copy || n < 3) return;
  const double k = stage_unit() * std::ldexp(1.0, shift);
  Prim& p = add_prim(GL_TRIANGLES, o);
  for (int i = 1; i + 1 < n; ++i)
    for (int j : {0, i, i + 1}) push(v[j][0] * k, v[j][1] * k, v[j][2] * k);
  p.count = GLsizei(st.verts.size() / 3) - p.first;
}

void note_line(const double a[3], const double b[3], const Ops& o) {
  if (!capture_ok() || o.copy) return;
  const double k = stage_unit();
  Prim& p = add_prim(GL_LINES, o);
  push(a[0] * k, a[1] * k, a[2] * k);
  push(b[0] * k, b[1] * k, b[2] * k);
  p.count = 2;
}

void note_point(const double q[3], int kind, int radius, const Ops& o) {
  if (!capture_ok() || o.copy) return;
  const double k = stage_unit();
  const double x = q[0] * k, z = q[2] * k;
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
  if (!st.ready || !st.capturing) return false;
  st.capturing = false;
  st.make_current();
  if (!ensure_fbo(w, h)) return false;
  const int rows = h / S;
  gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
  gl.Viewport(0, 0, w, h);
  gl.UseProgram(st.prog);
  gl.Enable(GL_PROGRAM_POINT_SIZE);
  const float log_far = 1.f / 40.f;  // log2 of the farthest depth
  auto proj = [&](float point_size) { gl.Uniform4f(st.u_proj, float(rows), point_size, 64.f, 0.f); };
  proj(float(S));
  gl.ColorMask(1, 1, 1, 1);
  gl.DepthMask(1);
  gl.ClearColor(5.f / 255.f, 0, 0, 1);  // sky (plane 0 on the land backdrop)
  gl.ClearDepth(1.0);
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  gl.ColorMask(1, 0, 0, 0);
  gl.LineWidth(float(std::max(1, S)));

  // ---- ground layer: land plane and water in world space ----
  float view[9];
  for (int i = 0; i < 9; ++i) view[i] = float(int16_t(rd16(0x5aed0 + 2u * uint32_t(i)))) / 256.f;
  // camera = cell origin - $5ad5a (the frame of the object positions)
  const double cam_x = double(int32_t(rd16(0x5ac44)) << 22) - double(int32_t(rd32(0x5ad5a)));
  const double cam_z = double(int32_t(rd16(0x5ac46)) << 22) - double(int32_t(rd32(0x5ad62)));
  const double ground_y = double(int32_t(rd32(0x5ad5e)));
  const int32_t alt = terrain::scaled_altitude(int32_t(rd32(0x5ad70)), rd8(0x5aad5),
                                               int16_t(rd16(0x5ad3a)));
  gl.Disable(GL_DEPTH_TEST);
  gl.DepthMask(0);
  gl.Uniform4f(st.u_depth, log_far, 0, 0, 0);
  std::vector<float> g;
  const double R = std::ldexp(1.0, 31);
  const double quad[6][2] = {{-R, -R}, {R, -R}, {R, R}, {-R, -R}, {R, R}, {-R, R}};
  for (auto& q : quad) {
    g.push_back(float(q[0]));
    g.push_back(float(ground_y));
    g.push_back(float(q[1]));
  }
  const GLsizei land = GLsizei(g.size() / 3);
  for (const State::WaterTri& t : st.water) {
    if (t.min_alt > alt) continue;
    for (int i = 0; i < 3; ++i) {
      g.push_back(float(t.x[i] - cam_x));
      g.push_back(float(ground_y));
      g.push_back(float(t.z[i] - cam_z));
    }
  }
  gl.BindBuffer(GL_ARRAY_BUFFER, st.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(g.size() * sizeof(float)), g.data(), GL_STREAM_DRAW);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);
  gl.EnableVertexAttribArray(0);
  set_transform(view);
  set_colour(4);
  gl.DrawArrays(GL_TRIANGLES, 0, land);
  Ops water;
  if (rd8(0x5aaa8)) water.set = 6, water.clr = 9;  // colour $5ac4c in all planes
  else water.set = 2;                              // plane 1 set
  draw_ops(GL_TRIANGLES, land, GLsizei(g.size() / 3) - land, water, false);

  // ---- objects: the original's primitives, already in view space ----
  const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  set_transform(ident);
  gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(st.verts.size() * sizeof(float)), st.verts.data(),
                GL_STREAM_DRAW);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);
  auto draw = [&](const Prim& p, bool depth) {
    gl.Uniform4f(st.u_depth, log_far, p.bias, 0, 0);
    if (p.mode == GL_POINTS) proj(p.size * float(S));
    draw_ops(p.mode, p.first, p.count, p.ops, depth);
  };
  for (const Prim& p : st.prims)
    if (p.flat) draw(p, false);
  gl.Enable(GL_DEPTH_TEST);
  gl.DepthFunc(GL_LEQUAL);
  gl.DepthMask(1);
  for (const Prim& p : st.prims)
    if (!p.flat) draw(p, true);
  gl.Disable(GL_DEPTH_TEST);

  // ---- read back the palette indices ----
  std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
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
