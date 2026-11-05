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
#include "native/gl_palette.hpp"
#include "native/gl_line_quad.hpp"
#include "native/remaster.hpp"

#include "native/runtime.hpp"
#include "native/terrain.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>

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

constexpr GLenum GL_TRIANGLES = 0x0004, GL_POINTS = 0x0000;
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
constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_NEAREST = 0x2600, GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11, GL_BLEND = 0x0BE2, GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303;

struct GL {
  void (*GenFramebuffers)(GLsizei, GLuint*);
  void (*BindFramebuffer)(GLenum, GLuint);
  void (*GenRenderbuffers)(GLsizei, GLuint*);
  void (*BindRenderbuffer)(GLenum, GLuint);
  void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
  void (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
  void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
  GLenum (*CheckFramebufferStatus)(GLenum);
  void (*DeleteRenderbuffers)(GLsizei, const GLuint*);
  void (*DeleteFramebuffers)(GLsizei, const GLuint*);
  void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
  void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
  void (*ClearStencil)(GLint);
  void (*Clear)(GLbitfield);
  void (*Enable)(GLenum);
  void (*Disable)(GLenum);
  void (*BlendFunc)(GLenum, GLenum);
  void (*DepthFunc)(GLenum);
  void (*DepthMask)(GLboolean);
  void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
  void (*StencilFunc)(GLenum, GLint, GLuint);
  void (*StencilOp)(GLenum, GLenum, GLenum);
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
  void (*Uniform1f)(GLint, GLfloat);
  void (*UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
  void (*GenBuffers)(GLsizei, GLuint*);
  void (*BindBuffer)(GLenum, GLuint);
  void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
  void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
  void (*EnableVertexAttribArray)(GLuint);
  void (*DisableVertexAttribArray)(GLuint);
  void (*DrawArrays)(GLenum, GLint, GLsizei);
  void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
  void (*PixelStorei)(GLenum, GLint);
  void (*GenTextures)(GLsizei, GLuint*);
  void (*DeleteTextures)(GLsizei, const GLuint*);
  void (*BindTexture)(GLenum, GLuint);
  void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
  void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
  void (*TexParameteri)(GLenum, GLenum, GLint);
  void (*ActiveTexture)(GLenum);
  void (*CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
  void (*Uniform1i)(GLint, GLint);
  void (*Uniform2f)(GLint, GLfloat, GLfloat);
  GLenum (*GetError)();
  const unsigned char* (*GetString)(GLenum);
};

GL gl;
decltype(GL::DrawArrays) raw_draw_arrays = nullptr;
uint64_t frame_draw_calls = 0, frame_draw_vertices = 0;

void counted_draw_arrays(GLenum mode, GLint first, GLsizei count) {
  ++frame_draw_calls;
  frame_draw_vertices += uint64_t(std::max(0, count));
  raw_draw_arrays(mode, first, count);
}

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
  load(p, gl.FramebufferTexture2D, "glFramebufferTexture2D", m);
  load(p, gl.CheckFramebufferStatus, "glCheckFramebufferStatus", m);
  load(p, gl.DeleteRenderbuffers, "glDeleteRenderbuffers", m);
  load(p, gl.DeleteFramebuffers, "glDeleteFramebuffers", m);
  load(p, gl.Viewport, "glViewport", m);
  load(p, gl.ClearColor, "glClearColor", m);
  load(p, gl.ClearStencil, "glClearStencil", m);
  load(p, gl.Clear, "glClear", m);
  load(p, gl.Enable, "glEnable", m);
  load(p, gl.Disable, "glDisable", m);
  load(p, gl.BlendFunc, "glBlendFunc", m);
  load(p, gl.DepthFunc, "glDepthFunc", m);
  load(p, gl.DepthMask, "glDepthMask", m);
  load(p, gl.ColorMask, "glColorMask", m);
  load(p, gl.StencilFunc, "glStencilFunc", m);
  load(p, gl.StencilOp, "glStencilOp", m);
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
  load(p, gl.Uniform1f, "glUniform1f", m);
  load(p, gl.UniformMatrix3fv, "glUniformMatrix3fv", m);
  load(p, gl.GenBuffers, "glGenBuffers", m);
  load(p, gl.BindBuffer, "glBindBuffer", m);
  load(p, gl.BufferData, "glBufferData", m);
  load(p, gl.VertexAttribPointer, "glVertexAttribPointer", m);
  load(p, gl.EnableVertexAttribArray, "glEnableVertexAttribArray", m);
  load(p, gl.DisableVertexAttribArray, "glDisableVertexAttribArray", m);
  load(p, gl.DrawArrays, "glDrawArrays", m);
  load(p, gl.ReadPixels, "glReadPixels", m);
  load(p, gl.PixelStorei, "glPixelStorei", m);
  load(p, gl.GenTextures, "glGenTextures", m);
  load(p, gl.DeleteTextures, "glDeleteTextures", m);
  load(p, gl.BindTexture, "glBindTexture", m);
  load(p, gl.TexImage2D, "glTexImage2D", m);
  load(p, gl.TexSubImage2D, "glTexSubImage2D", m);
  load(p, gl.TexParameteri, "glTexParameteri", m);
  load(p, gl.ActiveTexture, "glActiveTexture", m);
  load(p, gl.CopyTexSubImage2D, "glCopyTexSubImage2D", m);
  load(p, gl.Uniform1i, "glUniform1i", m);
  load(p, gl.Uniform2f, "glUniform2f", m);
  load(p, gl.GetError, "glGetError", m);
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
attribute vec2 a_style;
uniform mat3 u_view;
uniform vec4 u_param;
uniform float u_xscale;
uniform float u_water_class;
varying float v_palette;
void main() {
  vec3 v = u_view * a_pos;
  float rows = u_param.x;
  float zlin = v.z - 128.0;
  float zlog = (log2(max(v.z, 1.0e-6)) * (2.0 / 40.0) - 1.0 - u_param.w - a_style.y) * v.z;
  gl_Position = vec4((-0.003125 * v.z - v.x) * u_xscale,
                     (1.0 - 179.0 / rows) * v.z + (180.0 / rows) * v.y,
                     mix(zlin, zlog, u_param.z), v.z);
  gl_PointSize = u_param.y;
  v_palette = mix(a_style.x, a_style.y, u_water_class);
}
)";

const char* kFragment = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform vec4 u_colour;
uniform float u_coloured;
varying float v_palette;
void main() { gl_FragColor = vec4(mix(u_colour.r, v_palette, u_coloured), 0.0, 0.0, 1.0); }
)";

// The original Amiga screen is only needed where 2D drawing has changed a
// pixel since the geometry capture. The 3D scene is a palette-index texture,
// with row-local colours and an EHB bit (the original copper palette can
// change between scanlines). Scene rows need remapping for double buffering.
const char* kPresentVertex = R"(
attribute vec2 a_pos;
varying vec2 v_uv;
void main() {
  v_uv = vec2((a_pos.x + 1.0) * 0.5, (1.0 - a_pos.y) * 0.5);
  gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";
// Lightweight full-frame presenter for original 2D views, debug menus and
// unavoidable CPU-composition fallbacks. Both sources are top-down RGBA;
// the texture coordinates deliberately match the direct scene compositor.
const char* kBitmapFragment = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D u_image;
varying vec2 v_uv;
void main() { gl_FragColor = texture2D(u_image, v_uv); }
)";
const char* kPresentFragment = R"(
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#endif
uniform sampler2D u_scene, u_overlay, u_palette, u_rows, u_hud, u_pause, u_cockpit;
uniform vec2 u_dimensions;  // display width in source pixels; scene rows
uniform float u_side, u_scale, u_hud_visible, u_pause_visible;
uniform vec4 u_pause_ink; // live copper pen 9, visibility from original title
uniform vec4 u_reticle;  // live source-screen RGB, then visibility
uniform vec4 u_fuel_strokes0; // first four fuel-LCD seven-stroke masks
uniform float u_fuel_stroke4;
uniform vec4 u_fuel_ink; // live LCD foreground RGB and vector visibility
uniform vec4 u_fuel_bar; // live pen-4 RGB and native vertical-bar visibility
uniform float u_fuel_bar_rows; // integer height captured from displayed fuel
uniform vec4 u_radar_guide, u_radar_marker, u_radar_contact;
uniform vec2 u_radar_dot_pos; // validated location of the original moving dot
uniform vec4 u_radar_stencil_pos; // guide start y, marker top x/y from original writers
uniform vec4 u_frame_top, u_frame_corner, u_frame_rail; // live source pen colours
uniform vec4 u_digits0, u_digits1, u_digits2; // captured seven-stroke masks
uniform vec4 u_digit_ink0, u_digit_ink1, u_digit_ink2, u_digit_ink3;
uniform vec4 u_labels0; // K,T,F,T visibility (validated 3x5 source glyphs)
uniform float u_label_g;
uniform vec4 u_label_ink0, u_label_ink1, u_label_ink2, u_label_ink3, u_label_ink4;
uniform vec4 u_heading_ink; // heading scale/index live colour and visibility
uniform float u_heading_x;  // first major tick, observed from source stencil
varying vec2 v_uv;
float dist_segment(vec2 p, vec2 a, vec2 b) {
  vec2 d = b - a;
  return length(p - (a + d * clamp(dot(p-a,d)/dot(d,d), 0.0, 1.0)));
}
float digit_bit(float mask, float bit) { return mod(floor(mask / bit), 2.0); }
float digit_coverage(vec2 p, vec2 xy, float mask) {
  if (mask < 0.5) return 0.0;
  float l = (xy.x + 0.5) * 3.0, r = (xy.x + 2.5) * 3.0;
  float mx = (xy.x + 1.5) * 3.0;
  float t = (xy.y + 0.5) * 3.6, m = (xy.y + 2.5) * 3.6;
  float b = (xy.y + 4.5) * 3.6;
  if (mask > 127.5) return clamp(1.9 - dist_segment(p, vec2(mx,t), vec2(mx,b)), 0.0, 1.0);
  float d = 1e6;
  if (digit_bit(mask, 1.0) > 0.5)  d = min(d, dist_segment(p, vec2(l,t), vec2(r,t)));
  if (digit_bit(mask, 2.0) > 0.5)  d = min(d, dist_segment(p, vec2(r,t), vec2(r,m)));
  if (digit_bit(mask, 4.0) > 0.5)  d = min(d, dist_segment(p, vec2(r,m), vec2(r,b)));
  if (digit_bit(mask, 8.0) > 0.5)  d = min(d, dist_segment(p, vec2(l,b), vec2(r,b)));
  if (digit_bit(mask, 16.0) > 0.5) d = min(d, dist_segment(p, vec2(l,m), vec2(l,b)));
  if (digit_bit(mask, 32.0) > 0.5) d = min(d, dist_segment(p, vec2(l,t), vec2(l,m)));
  if (digit_bit(mask, 64.0) > 0.5) d = min(d, dist_segment(p, vec2(l,m), vec2(r,m)));
  return clamp(1.9 - d, 0.0, 1.0);
}
vec3 draw_digit(vec3 base, vec2 p, vec2 xy, float mask, vec3 ink) {
  return mix(base, ink, digit_coverage(p, xy, mask));
}
float letter_coverage(vec2 p, vec2 xy, float visible, float glyph) {
  if (visible < 0.5) return 0.0;
  float l = (xy.x + 0.5) * 3.0, m = (xy.x + 1.5) * 3.0;
  float r = (xy.x + 2.5) * 3.0;
  float t = (xy.y + 0.5) * 3.6, c = (xy.y + 2.5) * 3.6;
  float b = (xy.y + 4.5) * 3.6;
  float d = 1e6;
  if (glyph < 1.5) { // K
    d = min(d, dist_segment(p, vec2(l,t), vec2(l,b)));
    d = min(d, dist_segment(p, vec2(r,t), vec2(l,c)));
    d = min(d, dist_segment(p, vec2(l,c), vec2(r,b)));
  } else if (glyph < 2.5) { // T
    d = min(dist_segment(p, vec2(l,t), vec2(r,t)),
            dist_segment(p, vec2(m,t), vec2(m,b)));
  } else if (glyph < 3.5) { // F
    d = min(dist_segment(p, vec2(l,t), vec2(l,b)),
            min(dist_segment(p, vec2(l,t), vec2(r,t)),
                dist_segment(p, vec2(l,c), vec2(r,c))));
  } else { // G: the source font opens the upper-left and lower-left corners
    d = min(d, dist_segment(p, vec2(r,t), vec2(m,t)));
    d = min(d, dist_segment(p, vec2(m,t), vec2(l,t+3.6)));
    d = min(d, dist_segment(p, vec2(l,t+3.6), vec2(l,b-3.6)));
    d = min(d, dist_segment(p, vec2(l,b-3.6), vec2(m,b)));
    d = min(d, dist_segment(p, vec2(m,b), vec2(r,b)));
    d = min(d, dist_segment(p, vec2(r,c), vec2(r,b-3.6)));
  }
  return clamp(1.9 - d, 0.0, 1.0);
}
vec3 draw_letter(vec3 base, vec2 p, vec2 xy, float visible, float glyph, vec3 ink) {
  return mix(base, ink, letter_coverage(p, xy, visible, glyph));
}
float heading_coverage(vec2 p, float first_x) {
  float upper = 60.5 * 3.6, lower = 61.5 * 3.6;
  float d = dist_segment(p, vec2(159.5*3.0, 63.5*3.6),
                        vec2(159.5*3.0, 65.5*3.6));
  for (int i = 0; i < 5; ++i) {
    float xx = (first_x + float(i)*10.0 + 0.5)*3.0;
    if (mod(float(i), 2.0) < 0.5)
      d = min(d, dist_segment(p, vec2(xx,upper), vec2(xx,lower)));
    else
      d = min(d, distance(p, vec2(xx,lower)));
  }
  return clamp(1.7 - d, 0.0, 1.0);
}
void main() {
  float x = floor(v_uv.x * u_dimensions.x);
  float y = min(199.0, floor(v_uv.y * 200.0));
  float centre_x = x - u_side;
  vec4 row = texture2D(u_rows, vec2(0.5, (y + 0.5) / 200.0));
  float ehb = 0.0;
  vec3 base_colour;
  vec4 readout_glyph = vec4(0.0);
  if (centre_x >= 0.0 && centre_x < 320.0) {
    vec4 overlay = texture2D(u_overlay,
      vec2((centre_x + 0.5) / 320.0, (y + 0.5) / 200.0));
    if (overlay.a > 0.5) { base_colour = overlay.rgb; }
    else if (overlay.a > 0.32) {
      // Alpha 96/112 distinguishes a completed native 3x5 readout from
      // opaque source pixels (255) and indexed cockpit artwork (32/48).
      // RGB contains the stroke mask and the source-cell origin in the
      // 164x35 readout rectangle. No new sampler or GL extension required.
      readout_glyph = overlay;
      float bg_pen = overlay.a > 0.41 ? 5.0 : 0.0;
      base_colour = texture2D(u_palette,
        vec2((bg_pen + 0.5) / 32.0, (y + 0.5) / 200.0)).rgb;
    }
    else if (overlay.a > 0.10) {
      // 32/255 or 48/255 marks a static cockpit pixel. The source is the original
      // ADF ILBM, not a copy of the emulated framebuffer. Its 5-bit colour
      // index is resolved through the current per-row copper palette.
      vec4 cockpit = texture2D(u_cockpit,
        vec2((centre_x + 0.5) / 320.0, (y + 0.5) / 200.0));
      if (overlay.a > 0.17) base_colour = cockpit.rgb;
      else {
        float idx = floor(cockpit.a * 255.0 + 0.5);
        base_colour = texture2D(u_palette,
          vec2((idx + 0.5) / 32.0, (y + 0.5) / 200.0)).rgb;
      }
    }
    else {
      ehb = overlay.a > 0.03 ? 16.0 : 0.0;
      if (row.g < 0.5) base_colour = vec3(0.0);
      else {
        float scene_y = (floor(row.r * 255.0 + 0.5) + fract(v_uv.y * 200.0)) * u_scale;
        float tex_y = 1.0 - scene_y / (u_dimensions.y * u_scale);
        float idx = floor(texture2D(u_scene, vec2(v_uv.x, tex_y)).r * 15.0 + 0.5);
        base_colour = texture2D(u_palette,
          vec2((idx + ehb + 0.5) / 32.0, (y + 0.5) / 200.0)).rgb;
      }
    }
  } else {
    ehb = (centre_x < 0.0 ? row.b : row.a) > 0.5 ? 16.0 : 0.0;
    if (row.g < 0.5) base_colour = vec3(0.0);
    else {
      float scene_y = (floor(row.r * 255.0 + 0.5) + fract(v_uv.y * 200.0)) * u_scale;
      float tex_y = 1.0 - scene_y / (u_dimensions.y * u_scale);
      float idx = floor(texture2D(u_scene, vec2(v_uv.x, tex_y)).r * 15.0 + 0.5);
      base_colour = texture2D(u_palette,
        vec2((idx + ehb + 0.5) / 32.0, (y + 0.5) / 200.0)).rgb;
    }
  }
  // 960x720 native-resolution HUD, anchored to the *left viewport edge*,
  // rather than to the original 320-column 3D centre. The CPU path uses
  // exactly the same coordinate transform.
  if (u_hud_visible > 0.5 && x < 320.0) {
    vec4 hud = texture2D(u_hud, vec2(v_uv.x * u_dimensions.x / 320.0, v_uv.y));
    base_colour = mix(base_colour, hud.rgb, hud.a);
  }
  // Unlike the performance overlay, flight UI stays centred on the original
  // 4:3 view, even when widescreen adds scenery to either side.
  if (u_pause_visible > 0.5 && centre_x >= 0.0 && centre_x < 320.0) {
    vec4 ui = texture2D(u_pause, vec2((centre_x + fract(v_uv.x * u_dimensions.x)) / 320.0,
                                       v_uv.y));
    base_colour = mix(base_colour, u_pause_ink.rgb, ui.a);
  }
  // The original 4x23 fuel fill was removed from the overlay. Its height
  // is calculated from the same double-buffered fuel latch as the LCD,
  // then the narrow bar is antialiased at the actual output resolution.
  if (u_fuel_bar.a > 0.5 && centre_x >= 111.0 && centre_x < 115.0 &&
      y >= 158.0 && y < 181.0) {
    vec2 p = vec2(v_uv.x * u_dimensions.x - u_side, v_uv.y * 200.0);
    float top = 181.0 - u_fuel_bar_rows;
    float cov = clamp((p.x - 111.0) * u_scale, 0.0, 1.0) *
                clamp((115.0 - p.x) * u_scale, 0.0, 1.0) *
                clamp((p.y - top) * u_scale, 0.0, 1.0) *
                clamp((181.0 - p.y) * u_scale, 0.0, 1.0);
    base_colour = mix(base_colour, u_fuel_bar.rgb, cov);
  }
  // Central radar glyphs are subpixel native geometry, using original pen
  // colours but no source-resolution dot/line pixels or additional textures.
  if (u_radar_guide.a > 0.5 && centre_x >= 135.0 && centre_x < 182.0 &&
      y >= 146.0 && y < 182.0) {
    vec2 source = vec2(v_uv.x * u_dimensions.x - u_side, v_uv.y * 200.0);
    // GL drawable dimensions need not divide evenly into logical 3x pixels.
    // Snap to those pixel centres so the direct shader and CPU composer
    // antialias exactly the same vector samples despite viewport rounding.
    vec2 p = (floor(source * u_scale) + vec2(0.5)) / u_scale;
    float gy = u_radar_stencil_pos.x + 0.5;
    vec2 m = u_radar_stencil_pos.yz + vec2(0.5);
    float dg = min(dist_segment(p, vec2(139.5,gy), vec2(154.5,gy+15.0)),
                   dist_segment(p, vec2(177.5,gy), vec2(162.5,gy+15.0)));
    float dm = min(dist_segment(p, m, m + vec2(-1.0,1.0)),
                   dist_segment(p, m + vec2(-1.0,1.0), m + vec2(1.0,1.0)));
    float dc = distance(p, u_radar_dot_pos + vec2(0.5));
    base_colour = mix(base_colour, u_radar_guide.rgb, clamp(1.0 - dg*u_scale, 0.0, 1.0));
    base_colour = mix(base_colour, u_radar_marker.rgb, clamp(1.0 - dm*u_scale, 0.0, 1.0));
    base_colour = mix(base_colour, u_radar_contact.rgb, clamp(1.0 - dc*u_scale, 0.0, 1.0));
  }
  // The fuel LCD's original 19x5 text cells have already been replaced by
  // native panel backing in u_overlay. Their five captured values are now
  // resolution-independent seven-stroke digits, not enlarged bitplane dots.
  if (u_fuel_ink.a > 0.5 && centre_x >= 104.0 && centre_x < 123.0 &&
      y >= 183.0 && y < 188.0 && mod(floor(centre_x - 104.0), 4.0) < 3.0) {
    float n = floor((centre_x - 104.0) / 4.0);
    float mask = n < 0.5 ? u_fuel_strokes0.x :
                 n < 1.5 ? u_fuel_strokes0.y :
                 n < 2.5 ? u_fuel_strokes0.z :
                 n < 3.5 ? u_fuel_strokes0.w : u_fuel_stroke4;
    vec2 p = vec2((v_uv.x * u_dimensions.x - u_side) * 3.0, v_uv.y * 720.0);
    base_colour = draw_digit(base_colour, p, vec2(104.0 + n * 4.0, 183.0),
                             mask, u_fuel_ink.rgb);
  }
  if (readout_glyph.a > 0.32) {
    vec2 origin = vec2(144.0, 158.0) +
      floor(readout_glyph.gb * 255.0 + vec2(0.5));
    float mask = floor(readout_glyph.r * 255.0 + 0.5);
    float ink_pen = readout_glyph.a > 0.41 ? 0.0 : 4.0;
    vec3 ink = texture2D(u_palette,
      vec2((ink_pen + 0.5) / 32.0, (y + 0.5) / 200.0)).rgb;
    vec2 p = vec2((v_uv.x * u_dimensions.x - u_side) * 3.0, v_uv.y * 720.0);
    base_colour = draw_digit(base_colour, p, origin, mask, ink);
  }
  // Native 4:3-corrected antialiased crosshair, independent of original
  // bitplane glyphs. Alpha is the geometric coverage in 960x720 space.
  if (u_reticle.a > 0.5 && centre_x >= 155.0 && centre_x < 162.0 &&
      y >= 70.0 && y < 77.0) {
    vec2 p = vec2((v_uv.x * u_dimensions.x - u_side) * 3.0, v_uv.y * 720.0);
    float d = min(dist_segment(p, vec2(478.5, 257.4), vec2(478.5, 271.8)),
                  dist_segment(p, vec2(472.5, 264.6), vec2(484.5, 264.6)));
    base_colour = mix(base_colour, u_reticle.rgb, clamp(1.8 - d, 0.0, 1.0));
  }
  if (u_frame_rail.a > 0.5 &&
      ((centre_x >= 83.0 && centre_x < 88.0) ||
       (centre_x >= 231.0 && centre_x < 236.0)) &&
      y >= 45.0 && y < 146.0) {
    vec2 p = vec2((v_uv.x * u_dimensions.x - u_side) * 3.0, v_uv.y * 720.0);
    float left = dist_segment(p, vec2(256.5, 167.4), vec2(256.5, 520.2));
    float right = dist_segment(p, vec2(700.5, 167.4), vec2(700.5, 520.2));
    base_colour = mix(base_colour, u_frame_rail.rgb,
                      clamp(1.6 - min(left, right), 0.0, 1.0));
  }
  if (u_frame_top.a > 0.5 && centre_x >= 83.0 && centre_x < 236.0 &&
      y >= 35.0 && y < 48.0) {
    vec2 p = vec2((v_uv.x * u_dimensions.x - u_side) * 3.0, v_uv.y * 720.0);
    float top = dist_segment(p, vec2(280.5, 135.0), vec2(673.5, 135.0));
    float left = dist_segment(p, vec2(277.5, 138.6), vec2(256.5, 163.8));
    float right = dist_segment(p, vec2(676.5, 135.0), vec2(700.5, 163.8));
    base_colour = mix(base_colour, u_frame_top.rgb, clamp(1.6 - top, 0.0, 1.0));
    base_colour = mix(base_colour, u_frame_corner.rgb,
                      clamp(1.6 - min(left, right), 0.0, 1.0));
  }
  // Exact source-font recognition is the value/visibility oracle, while
  // these stroked numerals replace the blocky 320x200 glyphs at 960x720.
  if (centre_x >= 94.0 && centre_x < 231.0 &&
      ((y >= 52.0 && y < 59.0) || (y >= 83.0 && y < 91.0) ||
       (y >= 120.0 && y < 128.0))) {
    vec2 p = vec2((v_uv.x * u_dimensions.x - u_side) * 3.0, v_uv.y * 720.0);
    if (y >= 52.0 && y < 59.0) {
      if (centre_x >= 139.0 && centre_x < 145.0)
        base_colour = draw_digit(base_colour,p,vec2(140.0,53.0),u_digits0.x,u_digit_ink0.rgb);
      if (centre_x >= 143.0 && centre_x < 149.0)
        base_colour = draw_digit(base_colour,p,vec2(144.0,53.0),u_digits0.y,u_digit_ink0.rgb);
      if (centre_x >= 159.0 && centre_x < 165.0)
        base_colour = draw_digit(base_colour,p,vec2(160.0,53.0),u_digits0.z,u_digit_ink0.rgb);
      if (centre_x >= 163.0 && centre_x < 169.0)
        base_colour = draw_digit(base_colour,p,vec2(164.0,53.0),u_digits0.w,u_digit_ink0.rgb);
    } else if (y >= 83.0 && y < 91.0) {
      if (centre_x >= 97.0 && centre_x < 103.0)
        base_colour = draw_digit(base_colour,p,vec2(98.0,84.0),u_digits1.x,u_digit_ink1.rgb);
      if (centre_x >= 101.0 && centre_x < 107.0)
        base_colour = draw_digit(base_colour,p,vec2(102.0,84.0),u_digits1.y,u_digit_ink1.rgb);
      if (centre_x >= 105.0 && centre_x < 111.0)
        base_colour = draw_digit(base_colour,p,vec2(106.0,84.0),u_digits1.z,u_digit_ink1.rgb);
      if (centre_x >= 217.0 && centre_x < 223.0)
        base_colour = draw_digit(base_colour,p,vec2(218.0,84.0),u_digits1.w,u_digit_ink2.rgb);
      if (centre_x >= 221.0 && centre_x < 227.0)
        base_colour = draw_digit(base_colour,p,vec2(222.0,84.0),u_digits2.x,u_digit_ink2.rgb);
      if (centre_x >= 225.0 && centre_x < 231.0)
        base_colour = draw_digit(base_colour,p,vec2(226.0,84.0),u_digits2.y,u_digit_ink2.rgb);
    } else {
      if (centre_x >= 95.0 && centre_x < 101.0)
        base_colour = draw_digit(base_colour,p,vec2(96.0,121.0),u_digits2.z,u_digit_ink3.rgb);
      if (centre_x >= 101.0 && centre_x < 107.0)
        base_colour = draw_digit(base_colour,p,vec2(102.0,121.0),u_digits2.w,u_digit_ink3.rgb);
    }
  }
  if (centre_x >= 101.0 && centre_x < 232.0 &&
      ((y >= 90.0 && y < 97.0) || (y >= 120.0 && y < 127.0))) {
    // The visible window may scale a 1284x600 native image into 1280x718.
    // Resolve at the nearest source-layer subpixel, just like SDL's CPU
    // compositor, so tiny antialias edge weights remain GPU/CPU-identical.
    vec2 source = vec2(v_uv.x * u_dimensions.x - u_side, v_uv.y * 200.0);
    vec2 sample_xy = (floor(source * u_scale) + vec2(0.5)) / u_scale;
    vec2 p = sample_xy * vec2(3.0, 3.6);
    if (y < 97.0) {
      if (centre_x >= 101.0 && centre_x < 106.0)
        base_colour = draw_letter(base_colour,p,vec2(102.0,91.0),u_labels0.x,1.0,u_label_ink0.rgb);
      if (centre_x >= 105.0 && centre_x < 110.0)
        base_colour = draw_letter(base_colour,p,vec2(106.0,91.0),u_labels0.y,2.0,u_label_ink1.rgb);
      if (centre_x >= 221.0 && centre_x < 226.0)
        base_colour = draw_letter(base_colour,p,vec2(222.0,91.0),u_labels0.z,3.0,u_label_ink2.rgb);
      if (centre_x >= 225.0 && centre_x < 230.0)
        base_colour = draw_letter(base_colour,p,vec2(226.0,91.0),u_labels0.w,2.0,u_label_ink3.rgb);
    } else if (centre_x >= 107.0 && centre_x < 112.0) {
      base_colour = draw_letter(base_colour,p,vec2(108.0,121.0),u_label_g,4.0,u_label_ink4.rgb);
    }
  }
  if (u_heading_ink.a > 0.5 && centre_x >= 136.0 && centre_x < 192.0 &&
      y >= 59.0 && y < 67.0) {
    vec2 source = vec2(v_uv.x * u_dimensions.x - u_side, v_uv.y * 200.0);
    vec2 sample_xy = (floor(source * u_scale) + vec2(0.5)) / u_scale;
    float cov = heading_coverage(sample_xy * vec2(3.0,3.6), u_heading_x);
    base_colour = mix(base_colour, u_heading_ink.rgb, cov);
  }
  gl_FragColor = vec4(base_colour, 1.0);
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
  float size = 1;     // points: size in low-res pixels
  float bias = 0;     // depth bias (later primitives of an object win)
  bool flat = false;
  int object = -1;
  int sequence = 0;
  uint32_t mesh = 0, stream = 0;
};

struct Camera {
  float view[9]{};
  double x = 0, z = 0, ground_y = 0;
  int32_t alt = 0;
};

struct State {
  bool ready = false, enabled = true;
  bool deferred_scene_readback = false;
  std::function<void()> make_current;
  GLuint prog = 0, fbo = 0, colour_rb = 0, depth_rb = 0, stencil_rb = 0, vbo = 0;
  GLuint present_prog = 0, scene_tex = 0, overlay_tex = 0, palette_tex = 0, rows_tex = 0;
  GLuint bitmap_prog = 0, bitmap_tex = 0;
  int bitmap_tex_w = 0, bitmap_tex_h = 0;
  bool bitmap_failed = false;
  GLuint hud_tex = 0, pause_tex = 0, cockpit_tex = 0;
  bool present_failed = false;
  uint64_t uploaded_layers_revision = UINT64_MAX;
  bool scene_attached = false;  // scene texture is the FBO colour attachment
  int scene_tex_w = 0, scene_tex_h = 0;
  int fb_w = 0, fb_h = 0;
  GLint u_view = -1, u_param = -1, u_colour = -1, u_xscale = -1;
  GLint u_coloured = -1, u_water_class = -1;
  // current frame
  bool capturing = false;
  bool has_frame = false, has_previous = false;
  uint64_t serial = 0, capture_tick = 0;
  uint64_t last_rendered_serial = 0;
  int interval_vblanks = 4;
  int object = -1;          // index of the current object, -1 = none yet
  uint32_t mesh = 0, stream = 0;
  bool distant = false, flat = false;
  int prims_in_object = 0;
  std::vector<float> verts; // view space, world units
  std::vector<Prim> prims;
  std::vector<float> previous_verts;
  std::vector<Prim> previous_prims;
  uint64_t matched_serial = 0;
  std::vector<int> matched_previous_first; // one previous vertex offset per current primitive, or -1
  size_t matched_count = 0, matched_reordered = 0;
  Camera camera, previous_camera;
  // terrain: triangulated water, absolute world coordinates
  struct WaterTri { double x[3], z[3]; int32_t min_alt; };
  std::vector<WaterTri> water;
  std::vector<float> ground;  // per frame
  uint64_t frames = 0;
  bool debug = false;
};

State st;

// An original game tick may request a centre-size readback and a different
// widescreen presentation size. A single FBO would recreate *all* its colour,
// depth and stencil storage at each alternation. Cache a few complete sets
// (including the colour texture) instead; mobile GPUs have finite memory, so
// the cache is deliberately bounded rather than growing with viewport sizes.
struct CachedFbo {
  GLuint fbo, colour_rb, depth_rb, stencil_rb, scene_tex;
  int w, h, tex_w, tex_h;
  bool scene_attached;
};
std::vector<CachedFbo> dormant_fbos;

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
  p.object = st.object;
  p.mesh = st.mesh;
  p.stream = st.stream;
  p.sequence = st.prims_in_object++;
  p.bias = float(p.sequence) * 2e-6f;
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

void set_colour(uint8_t index) { gl.Uniform4f(st.u_colour, palette::encode(index), 0.f, 0.f, 1.f); }
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
  gl.DisableVertexAttribArray(1);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);
  gl.EnableVertexAttribArray(0);
}
void upload_coloured(const std::vector<float>& v) {
  gl.BindBuffer(GL_ARRAY_BUFFER, st.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, 5 * sizeof(float), nullptr);
  gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, 5 * sizeof(float),
                         reinterpret_cast<const void*>(3 * sizeof(float)));
  gl.EnableVertexAttribArray(0);
  gl.EnableVertexAttribArray(1);
}
void stencil_class(int water) {
  gl.StencilFunc(GL_EQUAL, water, 1);
  gl.StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

bool ensure_fbo(int w, int h) {
  if (st.fbo && st.fb_w == w && st.fb_h == h) return true;
  const auto snapshot = []() -> CachedFbo {
    return {st.fbo, st.colour_rb, st.depth_rb, st.stencil_rb, st.scene_tex,
            st.fb_w, st.fb_h, st.scene_tex_w, st.scene_tex_h, st.scene_attached};
  };
  const auto restore = [](const CachedFbo& saved) {
    st.fbo = saved.fbo;
    st.colour_rb = saved.colour_rb;
    st.depth_rb = saved.depth_rb;
    st.stencil_rb = saved.stencil_rb;
    st.scene_tex = saved.scene_tex;
    st.fb_w = saved.w;
    st.fb_h = saved.h;
    st.scene_tex_w = saved.tex_w;
    st.scene_tex_h = saved.tex_h;
    st.scene_attached = saved.scene_attached;
  };
  for (size_t i = 0; i < dormant_fbos.size(); ++i) {
    if (dormant_fbos[i].w != w || dormant_fbos[i].h != h) continue;
    const CachedFbo matching = dormant_fbos[i];
    dormant_fbos.erase(dormant_fbos.begin() + long(i));
    if (st.fbo) dormant_fbos.push_back(snapshot());
    restore(matching);
    gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
    return true;
  }
  if (st.fbo) {
    dormant_fbos.push_back(snapshot());
    if (dormant_fbos.size() > 3) {
      const CachedFbo retired = dormant_fbos.front();
      dormant_fbos.erase(dormant_fbos.begin());
      for (GLuint rb : {retired.colour_rb, retired.depth_rb, retired.stencil_rb})
        if (rb) gl.DeleteRenderbuffers(1, &rb);
      if (retired.scene_tex) gl.DeleteTextures(1, &retired.scene_tex);
      gl.DeleteFramebuffers(1, &retired.fbo);
    }
    restore({});
  }
  if (!st.fbo) gl.GenFramebuffers(1, &st.fbo);
  gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
  // On resize the old depth and stencil storage may have different sizes.
  // Detach it before checking a texture colour attachment (strict GLES2).
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
  gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
  GLuint old[3] = {st.colour_rb, st.depth_rb, st.stencil_rb};
  for (GLuint r : old)
    if (r) gl.DeleteRenderbuffers(1, &r);
  st.colour_rb = st.depth_rb = st.stencil_rb = 0;
  st.scene_attached = false;
  auto rb = [&](GLenum format) {
    GLuint r = 0;
    gl.GenRenderbuffers(1, &r);
    gl.BindRenderbuffer(GL_RENDERBUFFER, r);
    gl.RenderbufferStorage(GL_RENDERBUFFER, format, w, h);
    return r;
  };
  // Rendering directly into the texture sampled by the visible compositor
  // removes one full-size GPU-to-GPU copy per presentation. The unsized
  // GL_RGBA/UNSIGNED_BYTE texture format is legal on GL 2.1 and GLES2.
  // Preserve the explicit RGBA4 renderbuffer oracle and a fallback for
  // drivers that cannot complete an FBO with a colour texture.
  const bool force_rgba4 = std::getenv("FA18_GL_FORCE_RGBA4") != nullptr;
  const char* texture_opt = std::getenv("FA18_GL_SCENE_TEXTURE");
  if (!force_rgba4 && (!texture_opt || std::strcmp(texture_opt, "0") != 0)) {
    gl.ActiveTexture(GL_TEXTURE0);
    if (!st.scene_tex) gl.GenTextures(1, &st.scene_tex);
    gl.BindTexture(GL_TEXTURE_2D, st.scene_tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (st.scene_tex_w != w || st.scene_tex_h != h) {
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      st.scene_tex_w = w;
      st.scene_tex_h = h;
    }
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            st.scene_tex, 0);
    st.scene_attached = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  }
  if (!st.scene_attached) {
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    if (!force_rgba4) {
      st.colour_rb = rb(GL_RGBA8);
      gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                 st.colour_rb);
    }
    if (!st.colour_rb || gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      if (st.colour_rb) gl.DeleteRenderbuffers(1, &st.colour_rb);
      st.colour_rb = rb(GL_RGBA4);
      gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                 st.colour_rb);
    }
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
  if (std::getenv("FA18_GL_SCENE_DEBUG"))
    std::fprintf(stderr, "gl: scene FBO %dx%d colour=%s\n", w, h,
                 st.scene_attached ? "texture" : "renderbuffer");
  return true;
}

}  // namespace

// ---- public -------------------------------------------------------------------------

bool init(GetProc proc, std::function<void()> make_current, std::string& err) {
  st.make_current = std::move(make_current);
  st.make_current();
  if (!load_gl(proc, err)) return false;
  raw_draw_arrays = gl.DrawArrays;
  gl.DrawArrays = counted_draw_arrays;
  GLuint vs = compile(GL_VERTEX_SHADER, kVertex, err);
  if (!vs) return false;
  GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment, err);
  if (!fs) return false;
  st.prog = gl.CreateProgram();
  gl.AttachShader(st.prog, vs);
  gl.AttachShader(st.prog, fs);
  gl.BindAttribLocation(st.prog, 0, "a_pos");
  gl.BindAttribLocation(st.prog, 1, "a_style");
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
  st.u_xscale = gl.GetUniformLocation(st.prog, "u_xscale");
  st.u_coloured = gl.GetUniformLocation(st.prog, "u_coloured");
  st.u_water_class = gl.GetUniformLocation(st.prog, "u_water_class");
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
void set_deferred_scene_readback(bool on) { st.deferred_scene_readback = on; }
bool deferred_scene_readback() { return st.deferred_scene_readback; }

void scene_begin() {
  if (!active()) return;
  if (st.has_frame) {
    st.previous_verts = std::move(st.verts);
    st.previous_prims = std::move(st.prims);
    st.previous_camera = st.camera;
    st.has_previous = true;
  } else {
    st.has_previous = false;
  }
  st.has_frame = false;
  st.capturing = true;
  st.object = -1;
  st.verts.clear();
  st.prims.clear();
}

void cancel_capture() {
  if (!st.capturing) return;
  st.capturing = false;
  // scene_begin moved the completed frame aside to make room for a new
  // capture. A pause/menu transition can skip the matching scene_end;
  // restore the last complete geometry instead of treating a half-scene
  // (or no geometry at all) as the next presentation frame.
  if (!st.has_frame && st.has_previous) {
    st.verts = std::move(st.previous_verts);
    st.prims = std::move(st.previous_prims);
    st.camera = st.previous_camera;
    st.has_frame = true;
    st.has_previous = false;
    st.matched_serial = 0;
  }
}

void begin_object(uint32_t mesh, uint32_t stream, bool distant) {
  if (!active() || !st.capturing) return;
  ++st.object;
  st.mesh = mesh;
  st.stream = stream;
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
  using line_quad::Vec3;
  std::array<Vec3, 6> quad;
  // Screen-facing triangles avoid GLES2's optional wide GL_LINES. The
  // former 1/output-pixel strokes disappeared at high resolution; use a
  // restrained part of an original screen pixel with a physical minimum.
  const double pixel_width = line_quad::stroke_width(native::mach().hires_scale);
  if (!line_quad::make({pts[0].x, pts[0].y, pts[0].z},
                       {pts[1].x, pts[1].y, pts[1].z}, quad, pixel_width)) {
    Prim& p = add_prim(GL_TRIANGLES, o);
    p.size = float(pixel_width);
    for (const Vec3& v : line_quad::square({pts[0].x, pts[0].y, pts[0].z}, pixel_width))
      push(v.x, v.y, v.z);
    p.count = 6;
    return;
  }
  Prim& p = add_prim(GL_TRIANGLES, o);
  for (const Vec3& v : quad) push(v.x, v.y, v.z);
  p.count = 6;
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
  Prim& p = add_prim(GL_TRIANGLES, o);
  p.size = kind == 0 ? 1.f : 2.f;
  for (const line_quad::Vec3& v : line_quad::square({x, y, z}, p.size))
    push(v.x, v.y, v.z);
  p.count = 6;
}

Camera capture_camera() {
  Camera c;
  for (int i = 0; i < 9; ++i) c.view[i] = float(int16_t(rd16(0x5aed0 + 2u * uint32_t(i)))) / 256.f;
  c.x = double(int32_t(rd16(0x5ac44)) << 22) - double(int32_t(rd32(0x5ad5a)));
  c.z = double(int32_t(rd16(0x5ac46)) << 22) - double(int32_t(rd32(0x5ad62)));
  c.ground_y = double(int32_t(rd32(0x5ad5e)));
  c.alt = terrain::scaled_altitude(int32_t(rd32(0x5ad70)), rd8(0x5aad5),
                                    int16_t(rd16(0x5ad3a)));
  return c;
}

bool same_primitive(const Prim& a, const Prim& b) {
  return a.mode == b.mode && a.count == b.count && a.sequence == b.sequence &&
         a.mesh == b.mesh && a.stream == b.stream &&
         a.flat == b.flat && a.ops.set == b.ops.set && a.ops.clr == b.ops.clr &&
         a.ops.xr == b.ops.xr && a.ops.copy == b.ops.copy;
}

struct ObjectCentre {
  int ordinal = -1;
  uint32_t mesh = 0, stream = 0;
  bool flat = false;
  double sum[3]{};
  double count = 0;
};

std::vector<ObjectCentre> object_centres(const std::vector<Prim>& prims,
                                        const std::vector<float>& verts) {
  std::vector<ObjectCentre> objects;
  for (const Prim& p : prims) {
    if (objects.empty() || objects.back().ordinal != p.object) {
      ObjectCentre o;
      o.ordinal = p.object;
      o.mesh = p.mesh;
      o.stream = p.stream;
      o.flat = p.flat;
      objects.push_back(o);
    }
    ObjectCentre& o = objects.back();
    const size_t begin = size_t(p.first) * 3;
    const size_t end = begin + size_t(p.count) * 3;
    if (end > verts.size()) continue;
    for (size_t i = begin; i < end; i += 3) {
      for (int c = 0; c < 3; ++c) o.sum[c] += double(verts[i + size_t(c)]);
      o.count += 1;
    }
  }
  for (ObjectCentre& o : objects)
    if (o.count > 0) for (double& x : o.sum) x /= o.count;
  return objects;
}

// The original sorts its draw list by depth; an aircraft, runway marking or
// building can therefore change *ordinal* between otherwise identical 3D
// captures. Match whole object instances before matching their primitives.
// For repeated copies of one mesh, prefer the closest view-space centre and
// consume each previous instance at most once (no one-to-many morphing).
void build_primitive_matches() {
  st.matched_serial = st.serial;
  st.matched_previous_first.assign(st.prims.size(), -1);
  st.matched_count = st.matched_reordered = 0;
  const auto current_objects = object_centres(st.prims, st.verts);
  const auto old_objects = object_centres(st.previous_prims, st.previous_verts);
  struct Candidate { double distance; int delta, now, old; };
  std::vector<Candidate> candidates;
  for (size_t i = 0; i < current_objects.size(); ++i) {
    const ObjectCentre& a = current_objects[i];
    for (size_t j = 0; j < old_objects.size(); ++j) {
      const ObjectCentre& b = old_objects[j];
      if (a.mesh != b.mesh || a.stream != b.stream || a.flat != b.flat ||
          a.count == 0 || b.count == 0) continue;
      double dist = 0;
      for (int c = 0; c < 3; ++c) {
        const double d = a.sum[c] - b.sum[c];
        dist += d * d;
      }
      candidates.push_back({dist, std::abs(a.ordinal - b.ordinal), int(i), int(j)});
    }
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.distance != b.distance) return a.distance < b.distance;
    return a.delta < b.delta;
  });
  std::vector<int> object_map(st.object + 1, -1);
  std::vector<bool> used_old(old_objects.size(), false);
  for (const Candidate& c : candidates) {
    const int ordinal = current_objects[size_t(c.now)].ordinal;
    if (object_map[size_t(ordinal)] >= 0 || used_old[size_t(c.old)]) continue;
    object_map[size_t(ordinal)] = old_objects[size_t(c.old)].ordinal;
    used_old[size_t(c.old)] = true;
  }
  auto key = [](int object, int sequence) {
    return (uint64_t(uint32_t(object)) << 32) | uint32_t(sequence);
  };
  std::unordered_map<uint64_t, const Prim*> old_prims;
  old_prims.reserve(st.previous_prims.size());
  for (const Prim& p : st.previous_prims) old_prims.emplace(key(p.object, p.sequence), &p);
  for (size_t i = 0; i < st.prims.size(); ++i) {
    const Prim& p = st.prims[i];
    if (p.object < 0 || size_t(p.object) >= object_map.size()) continue;
    const int old_object = object_map[size_t(p.object)];
    if (old_object < 0) continue;
    auto it = old_prims.find(key(old_object, p.sequence));
    if (it == old_prims.end() || !same_primitive(p, *it->second)) continue;
    const Prim& old = *it->second;
    const size_t n = size_t(p.count) * 3;
    if (size_t(p.first) * 3 + n > st.verts.size() ||
        size_t(old.first) * 3 + n > st.previous_verts.size()) continue;
    st.matched_previous_first[i] = old.first;
    ++st.matched_count;
    if (old_object != p.object) ++st.matched_reordered;
  }
  if (std::getenv("FA18_GL_MATCH_DEBUG") && st.serial % 20 == 0)
    std::fprintf(stderr, "gl: match serial %llu current %zu previous %zu matched %zu reordered %zu\n",
                 (unsigned long long)st.serial, st.prims.size(), st.previous_prims.size(),
                 st.matched_count, st.matched_reordered);
}

bool interpolation_available() { return active() && st.has_frame && st.has_previous; }
uint64_t capture_serial() { return st.serial; }
int capture_interval_vblanks() { return st.interval_vblanks; }

void finish_capture() {
  if (!active() || !st.capturing) return;
  st.capturing = false;
  st.camera = capture_camera();
  st.has_frame = true;
  const uint64_t tick = native::mach().frame_count();
  if (st.serial > 0 && tick > st.capture_tick) {
    const uint64_t elapsed = tick - st.capture_tick;
    if (elapsed <= 12) st.interval_vblanks = int(elapsed);
    else st.has_previous = false;  // menu/pause: don't interpolate across a cut
  }
  st.capture_tick = tick;
  ++st.serial;
  ++st.frames;
}

static bool draw_layer(std::vector<uint8_t>* px, int w, int h, int S, float alpha) {
  if (!active() || (!st.capturing && !st.has_frame) || w < 320 * S || S <= 0) return false;
  if (st.capturing) {
    // VBL presentation can occur halfway through the next 3D draw.  The
    // completed scene was moved into previous_* by scene_begin(); drawing
    // (or finish_capture() here) would otherwise expose/finalise a partial
    // primitive list and make alternate widescreen frames go blank.  Keep
    // the capture intact and present the previous complete scene instead.
    if (!st.has_previous) return false;
    std::swap(st.verts, st.previous_verts);
    std::swap(st.prims, st.previous_prims);
    std::swap(st.camera, st.previous_camera);
    st.capturing = false;
    st.has_frame = true;
    st.has_previous = false;
    const bool rendered = draw_layer(px, w, h, S, 1.f);
    st.has_previous = true;
    st.has_frame = false;
    st.capturing = true;
    std::swap(st.camera, st.previous_camera);
    std::swap(st.prims, st.previous_prims);
    std::swap(st.verts, st.previous_verts);
    return rendered;
  }
  finish_capture();
  // A capture may be finalised at scene_end without rendering (deferred
  // widescreen/high-FPS path). Log/profile the first *render* of a capture,
  // not only a render that happened to finalise it.
  const bool first_render = st.serial != st.last_rendered_serial;
  alpha = std::clamp(alpha, 0.f, 1.f);
  const bool interpolate = st.has_previous && alpha < 1.f;
  size_t matched = 0;
  const std::vector<float>* verts = &st.verts;
  std::vector<float> interpolated;
  Camera cam = st.camera;
  if (interpolate) {
    interpolated = st.verts;
    // Cache the correspondence for the entire capture; high-FPS redraws
    // only need to blend the vertex arrays, not re-match the object list.
    if (st.matched_serial != st.serial) build_primitive_matches();
    for (size_t j = 0; j < st.prims.size(); ++j) {
      const Prim& p = st.prims[j];
      const int old_first = st.matched_previous_first[j];
      if (old_first < 0) continue;
      const size_t n = size_t(p.count) * 3;
      for (size_t i = 0; i < n; ++i) {
        const size_t at = size_t(p.first) * 3 + i;
        interpolated[at] = st.previous_verts[size_t(old_first) * 3 + i] +
                           alpha * (st.verts[at] - st.previous_verts[size_t(old_first) * 3 + i]);
      }
      ++matched;
    }
    verts = &interpolated;
    for (int i = 0; i < 9; ++i)
      cam.view[i] = st.previous_camera.view[i] + alpha * (st.camera.view[i] - st.previous_camera.view[i]);
    cam.x = st.previous_camera.x + alpha * (st.camera.x - st.previous_camera.x);
    cam.z = st.previous_camera.z + alpha * (st.camera.z - st.previous_camera.z);
    cam.ground_y = st.previous_camera.ground_y + alpha * (st.camera.ground_y - st.previous_camera.ground_y);
  }
  if (!first_render && std::getenv("FA18_GL_INTERP_DEBUG")) {
    static uint64_t redraws = 0, blended = 0, matched_total = 0;
    ++redraws;
    if (interpolate) ++blended;
    matched_total += matched;
    if (redraws % 120 == 0)
      std::fprintf(stderr, "gl: redraws %llu interpolated %llu matched-prims %llu alpha %.2f interval %d\n",
                   (unsigned long long)redraws, (unsigned long long)blended,
                   (unsigned long long)matched_total, alpha, st.interval_vblanks);
  }
  st.make_current();
  const bool profile = std::getenv("FA18_GL_PROFILE") != nullptr;
  const auto t0 = std::chrono::steady_clock::now();
  frame_draw_calls = frame_draw_vertices = 0;
  if (!ensure_fbo(w, h)) return false;
  const float rows = float(h / S);
  gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
  gl.Viewport(0, 0, w, h);
  gl.UseProgram(st.prog);
  gl.Uniform1f(st.u_coloured, 0.f);
  gl.Uniform1f(st.u_water_class, 0.f);
  // A wider FBO must reveal more of the world, not stretch the central view.
  // With w=320*S this is exactly 1.0 and preserves the original projection.
  gl.Uniform1f(st.u_xscale, float(320 * S) / float(w));
#ifndef FA18_OPENGLES2
  gl.Enable(GL_PROGRAM_POINT_SIZE);  // desktop GL; GLES always uses gl_PointSize
#endif
  gl.ColorMask(1, 1, 1, 1);
  gl.DepthMask(1);
  gl.ClearColor(palette::encode(5), 0, 0, 1);  // sky
  gl.ClearStencil(0);
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  gl.Disable(GL_DEPTH_TEST);
  gl.DepthMask(0);

  // ---- ground: land plane and water in world space ----
  // Camera = cell origin - $5ad5a (the frame of the object positions).
  std::vector<float>& g = st.ground;
  g.clear();
  const double R = std::ldexp(1.0, 31);
  const double quad[6][2] = {{-R, -R}, {R, -R}, {R, R}, {-R, -R}, {R, R}, {-R, R}};
  for (auto& q : quad) {
    g.push_back(float(q[0]));
    g.push_back(float(cam.ground_y));
    g.push_back(float(q[1]));
  }
  for (const State::WaterTri& t : st.water) {
    if (t.min_alt > cam.alt && !native::mach().full_detail) continue;
    for (int i = 0; i < 3; ++i) {
      g.push_back(float(t.x[i] - cam.x));
      g.push_back(float(cam.ground_y));
      g.push_back(float(t.z[i] - cam.z));
    }
  }
  upload(g);
  set_view(cam.view);
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
    // Stencil inversion is XOR: overlapping toggle triangles commute.
    // One ordered buffer has precisely the same result as one draw call
    // per island/marking primitive, with far less host/driver overhead.
    static std::vector<float> toggle_verts;
    toggle_verts.clear();
    for (const Prim& p : st.prims) {
      if (!toggles(p)) continue;
      const size_t at = size_t(p.first) * 3;
      toggle_verts.insert(toggle_verts.end(), verts->begin() + at,
                          verts->begin() + at + size_t(p.count) * 3);
    }
    upload(toggle_verts);
    set_view(ident);
    gl.StencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    gl.DrawArrays(GL_TRIANGLES, 0, GLsizei(toggle_verts.size() / 3));
    upload(g);
    set_view(cam.view);
  }
  gl.ColorMask(1, 0, 0, 0);
  stencil_class(0);
  set_colour(kLand);
  gl.DrawArrays(GL_TRIANGLES, 0, 6);
  stencil_class(1);
  set_colour(rd8(0x5aaa8) ? rd16(0x5ac4c) & 15 : kWater);
  gl.DrawArrays(GL_TRIANGLES, 0, 6);

  // ---- flat objects: preserve their order *within* each stencil class ----
  // The land and water classes never overlap and the stencil is fixed by
  // this point. They can be drawn in two ordered batches instead of doing
  // two draw calls + uniforms for each flat primitive. a_style stores
  // (palette-on-land, palette-on-water); u_water_class selects a channel.
  set_view(ident);
  auto point_size = [&](const Prim& p) { return p.mode == GL_POINTS ? p.size * float(S) : 1.f; };
  static std::vector<float> flat_styled;
  flat_styled.clear();
  for (const Prim& p : st.prims) {
    if (!p.flat || toggles(p)) continue;
    const float land = palette::encode(resolve(p.ops, kLand));
    const float water = palette::encode(resolve(p.ops, kWater));
    const size_t first = size_t(p.first) * 3;
    for (int j = 0; j < p.count; ++j) {
      const size_t at = first + size_t(j) * 3;
      flat_styled.insert(flat_styled.end(), {(*verts)[at], (*verts)[at + 1],
                                              (*verts)[at + 2], land, water});
    }
  }
  if (!flat_styled.empty()) {
    upload_coloured(flat_styled);
    set_param(rows, 1.f, 0.f, 0.f);
    gl.Uniform1f(st.u_coloured, 1.f);
    stencil_class(0);
    gl.DrawArrays(GL_TRIANGLES, 0, GLsizei(flat_styled.size() / 5));
    stencil_class(1);
    gl.Uniform1f(st.u_water_class, 1.f);
    gl.DrawArrays(GL_TRIANGLES, 0, GLsizei(flat_styled.size() / 5));
    gl.Uniform1f(st.u_water_class, 0.f);
    gl.Uniform1f(st.u_coloured, 0.f);
  }
  gl.Disable(GL_STENCIL_TEST);

  // ---- 3D objects: ordered, per-vertex coloured triangle batches ----
  // A separate GL draw for every polygon changed uniforms hundreds of
  // times on full detail. Consecutive triangle primitives can instead use
  // a shared stream: colour and the original per-primitive depth bias are
  // constant attributes for each triangle. Keep points in their original
  // draw order between the batches rather than moving them to the end.
  gl.Enable(GL_DEPTH_TEST);
  gl.DepthFunc(GL_LEQUAL);
  gl.DepthMask(1);
  static std::vector<float> styled;
  bool original_vertices_bound = true;
  for (size_t i = 0; i < st.prims.size();) {
    const Prim& p = st.prims[i];
    if (p.flat) { ++i; continue; }
    if (p.mode != GL_TRIANGLES) {
      if (!original_vertices_bound) { upload(*verts); original_vertices_bound = true; }
      set_param(rows, point_size(p), 1, p.bias);
      set_colour(resolve(p.ops, kLand));
      gl.DrawArrays(p.mode, p.first, p.count);
      ++i;
      continue;
    }
    styled.clear();
    while (i < st.prims.size()) {
      const Prim& t = st.prims[i];
      if (t.flat) { ++i; continue; }
      if (t.mode != GL_TRIANGLES) break;
      const float colour = palette::encode(resolve(t.ops, kLand));
      const size_t first = size_t(t.first) * 3;
      const size_t count = size_t(t.count);
      for (size_t j = 0; j < count; ++j) {
        const size_t at = first + j * 3;
        styled.insert(styled.end(), {(*verts)[at], (*verts)[at + 1], (*verts)[at + 2],
                                      colour, t.bias});
      }
      ++i;
    }
    upload_coloured(styled);
    original_vertices_bound = false;
    set_param(rows, 1.f, 1.f, 0.f);
    gl.Uniform1f(st.u_coloured, 1.f);
    gl.DrawArrays(GL_TRIANGLES, 0, GLsizei(styled.size() / 5));
    gl.Uniform1f(st.u_coloured, 0.f);
  }
  gl.Disable(GL_DEPTH_TEST);

  // ---- read back only when the caller needs a CPU frame ----
  const auto t1 = std::chrono::steady_clock::now();
  static std::vector<uint8_t> rgba;
  if (px) {
    rgba.resize(size_t(w) * size_t(h) * 4);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  }
  const auto t2 = std::chrono::steady_clock::now();
  if (px) {
    px->resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y) {
      const uint8_t* src = &rgba[size_t(h - 1 - y) * size_t(w) * 4];
      uint8_t* dst = &(*px)[size_t(y) * size_t(w)];
      for (int x = 0; x < w; ++x) dst[x] = palette::decode(src[x * 4]);
    }
  }
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  if (profile && first_render && (st.frames % 20) == 0) {
    const auto t3 = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::fprintf(stderr,
                 "gl: profile capture %llu prims %zu calls %llu vertices %llu "
                 "draw_ms %.2f read_ms %.2f convert_ms %.2f fb %dx%d\n",
                 (unsigned long long)st.frames, st.prims.size(),
                 (unsigned long long)frame_draw_calls,
                 (unsigned long long)frame_draw_vertices,
                 ms(t0, t1), ms(t1, t2), ms(t2, t3), w, h);
  }
  if (st.debug && first_render && (st.frames % 100) == 0)
    std::fprintf(stderr, "gl: frame %llu objects %d prims %zu\n", (unsigned long long)st.frames,
                 st.object + 1, st.prims.size());
  st.last_rendered_serial = st.serial;
  return true;
}

bool render_layer(std::vector<uint8_t>& px, int w, int h, int S, float alpha) {
  return draw_layer(&px, w, h, S, alpha);
}

bool present_gpu(const remaster::GpuLayers& layer, uint64_t layer_revision,
                 int side, int S, float alpha,
                 int drawable_w, int drawable_h,
                 const uint8_t* hud_rgba, bool hud_visible,
                 const uint8_t* pause_rgba, bool pause_visible,
                 const uint8_t* cockpit_rgba) {
  if (!active() || S <= 0 || side < 0 || layer.scene_rows <= 0 ||
      layer.overlay.size() != size_t(320 * 200 * 4) ||
      layer.palette.size() != size_t(32 * 200 * 4) ||
      layer.rows.size() != size_t(200 * 4) ||
      drawable_w <= 0 || drawable_h <= 0) return false;
  const remaster::NativeHudState hud_state = remaster::prepared_gpu_hud_state();
  st.make_current();
  if (st.present_failed) return false;
  const int wide = 320 + side * 2;
  const int width = wide * S, height = layer.scene_rows * S;

  if (!st.present_prog) {
    std::string err;
    const GLuint vs = compile(GL_VERTEX_SHADER, kPresentVertex, err);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kPresentFragment, err);
    if (!vs || !fs) {
      std::fprintf(stderr, "gl: GPU compositor unavailable (%s)\n", err.c_str());
      st.present_failed = true;
      return false;
    }
    st.present_prog = gl.CreateProgram();
    gl.AttachShader(st.present_prog, vs);
    gl.AttachShader(st.present_prog, fs);
    gl.BindAttribLocation(st.present_prog, 0, "a_pos");
    gl.LinkProgram(st.present_prog);
    GLint ok = 0;
    gl.GetProgramiv(st.present_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[2048] = {};
      gl.GetProgramInfoLog(st.present_prog, sizeof(log), nullptr, log);
      std::fprintf(stderr, "gl: GPU compositor link: %s\n", log);
      st.present_failed = true;
      return false;
    }
  }
  if (!draw_layer(nullptr, width, height, S, alpha)) return false;

  auto bind_texture = [&](GLuint& tex, int unit, int w, int h, const void* pixels,
                          bool upload_pixels = true) {
    gl.ActiveTexture(GL_TEXTURE0 + unit);
    const bool created = !tex;
    if (created) gl.GenTextures(1, &tex);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    if (created) {
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    // A null source explicitly reallocates a render-to-texture fallback
    // when the viewport dimensions change; it is not a reuse request.
    if (created || !pixels) {
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    } else if (upload_pixels && pixels) {
      gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
  };
  // The ordinary path already rendered into scene_tex as the scene FBO's
  // colour attachment. Only a strict RGBA4 / unsupported-texture fallback
  // needs the legacy renderbuffer-to-texture copy.
  if (!st.scene_attached) {
    if (!st.scene_tex || st.scene_tex_w != width || st.scene_tex_h != height) {
      bind_texture(st.scene_tex, 0, width, height, nullptr);
      st.scene_tex_w = width;
      st.scene_tex_h = height;
    } else {
      gl.ActiveTexture(GL_TEXTURE0);
      gl.BindTexture(GL_TEXTURE_2D, st.scene_tex);
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, st.fbo);
    gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  } else {
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, st.scene_tex);
  }
  const bool upload_layers = layer_revision != st.uploaded_layers_revision ||
                             !st.overlay_tex || !st.palette_tex || !st.rows_tex ||
                             std::getenv("FA18_GL_LAYER_UPLOAD_REFERENCE") != nullptr;
  // Extra high-refresh presents reproject fresh interpolated 3D geometry,
  // but the Amiga overlay, scanline copper palette and double-buffer row
  // mapping are already cached by the host for this 60-Hz game frame.
  // Always *bind* every unit: a bitmap-only transitional frame may have
  // switched the texture bindings since our previous GL scene present.
  bind_texture(st.overlay_tex, 1, 320, 200, layer.overlay.data(), upload_layers);
  bind_texture(st.palette_tex, 2, 32, 200, layer.palette.data(), upload_layers);
  bind_texture(st.rows_tex, 3, 1, 200, layer.rows.data(), upload_layers);
  st.uploaded_layers_revision = layer_revision;
  if (std::getenv("FA18_GL_LAYER_UPLOAD_DEBUG")) {
    static uint64_t uploaded = 0, reused = 0;
    if (upload_layers) ++uploaded;
    else ++reused;
    if (uploaded + reused <= 6 || (!upload_layers && reused <= 12) ||
        (uploaded + reused) % 30 == 0)
      std::fprintf(stderr, "gl: layer textures revision=%llu upload=%d uploaded=%llu reused=%llu\n",
                   (unsigned long long)layer_revision, int(upload_layers),
                   (unsigned long long)uploaded, (unsigned long long)reused);
  }
  if (hud_rgba) bind_texture(st.hud_tex, 4, 960, 720, hud_rgba);
  if (hud_visible && !st.hud_tex) return false;
  // The vector Pause lettering is immutable: retain its own GPU texture
  // across frames, and re-upload only if the GL context was recreated.
  if (pause_rgba && !st.pause_tex) bind_texture(st.pause_tex, 5, 960, 720, pause_rgba);
  if (pause_visible && !st.pause_tex) return false;
  if (cockpit_rgba && !st.cockpit_tex)
    bind_texture(st.cockpit_tex, 6, 320, 200, cockpit_rgba);
  if (!st.cockpit_tex) return false;

  gl.Viewport(0, 0, drawable_w, drawable_h);
  gl.ColorMask(1, 1, 1, 1);
  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  const double aspect = double(wide) / 240.0;
  const int vw = std::min(drawable_w, int(std::round(drawable_h * aspect)));
  const int vh = std::min(drawable_h, int(std::round(drawable_w / aspect)));
  gl.Viewport((drawable_w - vw) / 2, (drawable_h - vh) / 2, vw, vh);
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_BLEND);
  gl.Disable(GL_CULL_FACE);
  gl.UseProgram(st.present_prog);
  gl.Uniform1i(gl.GetUniformLocation(st.present_prog, "u_scene"), 0);
  gl.Uniform1i(gl.GetUniformLocation(st.present_prog, "u_overlay"), 1);
  gl.Uniform1i(gl.GetUniformLocation(st.present_prog, "u_palette"), 2);
  gl.Uniform1i(gl.GetUniformLocation(st.present_prog, "u_rows"), 3);
  gl.Uniform1i(gl.GetUniformLocation(st.present_prog, "u_hud"), 4);
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_hud_visible"), hud_visible ? 1.f : 0.f);
  gl.Uniform1i(gl.GetUniformLocation(st.present_prog, "u_pause"), 5);
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_pause_visible"),
               pause_visible && hud_state.pause_title ? 1.f : 0.f);
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_pause_ink"),
               float((hud_state.pause_rgb >> 16) & 255) / 255.f,
               float((hud_state.pause_rgb >> 8) & 255) / 255.f,
               float(hud_state.pause_rgb & 255) / 255.f,
               hud_state.pause_title ? 1.f : 0.f);
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_fuel_strokes0"),
               float(layer.fuel_strokes[0]), float(layer.fuel_strokes[1]),
               float(layer.fuel_strokes[2]), float(layer.fuel_strokes[3]));
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_fuel_stroke4"),
               float(layer.fuel_strokes[4]));
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_fuel_ink"),
               float((layer.fuel_ink_rgb >> 16) & 255) / 255.f,
               float((layer.fuel_ink_rgb >> 8) & 255) / 255.f,
               float(layer.fuel_ink_rgb & 255) / 255.f,
               layer.fuel_vector ? 1.f : 0.f);
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_fuel_bar"),
               float((layer.fuel_bar_ink_rgb >> 16) & 255) / 255.f,
               float((layer.fuel_bar_ink_rgb >> 8) & 255) / 255.f,
               float(layer.fuel_bar_ink_rgb & 255) / 255.f,
               layer.fuel_bar_vector ? 1.f : 0.f);
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_fuel_bar_rows"),
               float(layer.fuel_bar_rows));
  const auto radar_ink = [&](const char* uniform, uint32_t c) {
    gl.Uniform4f(gl.GetUniformLocation(st.present_prog, uniform),
                 float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f,
                 float(c & 255) / 255.f, layer.radar_vector ? 1.f : 0.f);
  };
  radar_ink("u_radar_guide", layer.radar_guide_rgb);
  radar_ink("u_radar_marker", layer.radar_marker_rgb);
  radar_ink("u_radar_contact", layer.radar_dot_rgb);
  gl.Uniform2f(gl.GetUniformLocation(st.present_prog, "u_radar_dot_pos"),
               float(layer.radar_dot_x), float(layer.radar_dot_y));
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_radar_stencil_pos"),
               float(layer.radar_guide_y), float(layer.radar_marker_x),
               float(layer.radar_marker_y), 0.f);
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_reticle"),
               float((hud_state.reticle_rgb >> 16) & 255) / 255.f,
               float((hud_state.reticle_rgb >> 8) & 255) / 255.f,
               float(hud_state.reticle_rgb & 255) / 255.f,
               hud_state.reticle ? 1.f : 0.f);
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_frame_top"),
               float((hud_state.frame_top_rgb >> 16) & 255) / 255.f,
               float((hud_state.frame_top_rgb >> 8) & 255) / 255.f,
               float(hud_state.frame_top_rgb & 255) / 255.f,
               hud_state.frame_crown ? 1.f : 0.f);
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_frame_corner"),
               float((hud_state.frame_corner_rgb >> 16) & 255) / 255.f,
               float((hud_state.frame_corner_rgb >> 8) & 255) / 255.f,
               float(hud_state.frame_corner_rgb & 255) / 255.f,
               hud_state.frame_crown ? 1.f : 0.f);
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_frame_rail"),
               float((hud_state.frame_rail_rgb >> 16) & 255) / 255.f,
               float((hud_state.frame_rail_rgb >> 8) & 255) / 255.f,
               float(hud_state.frame_rail_rgb & 255) / 255.f,
               hud_state.frame_rails ? 1.f : 0.f);
  for (int group = 0; group < 3; ++group) {
    const auto& m = hud_state.digit_strokes;
    const char* name = group == 0 ? "u_digits0" :
                       group == 1 ? "u_digits1" : "u_digits2";
    gl.Uniform4f(gl.GetUniformLocation(st.present_prog, name),
                 float(m[size_t(group * 4)]), float(m[size_t(group * 4 + 1)]),
                 float(m[size_t(group * 4 + 2)]), float(m[size_t(group * 4 + 3)]));
  }
  for (int group = 0; group < 4; ++group) {
    const uint32_t c = hud_state.digit_colours[size_t(group)];
    const char* name = group == 0 ? "u_digit_ink0" : group == 1 ? "u_digit_ink1" :
                       group == 2 ? "u_digit_ink2" : "u_digit_ink3";
    gl.Uniform4f(gl.GetUniformLocation(st.present_prog, name),
                 float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f,
                 float(c & 255) / 255.f, c ? 1.f : 0.f);
  }
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_labels0"),
               float(hud_state.letter_visible[0]), float(hud_state.letter_visible[1]),
               float(hud_state.letter_visible[2]), float(hud_state.letter_visible[3]));
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_label_g"),
               float(hud_state.letter_visible[4]));
  for (int letter = 0; letter < 5; ++letter) {
    const char* name = letter == 0 ? "u_label_ink0" : letter == 1 ? "u_label_ink1" :
                       letter == 2 ? "u_label_ink2" : letter == 3 ? "u_label_ink3" :
                                     "u_label_ink4";
    const uint32_t c = hud_state.letter_colours[size_t(letter)];
    gl.Uniform4f(gl.GetUniformLocation(st.present_prog, name),
                 float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f,
                 float(c & 255) / 255.f, c ? 1.f : 0.f);
  }
  const uint32_t heading = hud_state.heading_rgb;
  gl.Uniform4f(gl.GetUniformLocation(st.present_prog, "u_heading_ink"),
               float((heading >> 16) & 255) / 255.f,
               float((heading >> 8) & 255) / 255.f,
               float(heading & 255) / 255.f,
               hud_state.heading_marks ? 1.f : 0.f);
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_heading_x"),
               float(hud_state.heading_first_x));
  if (std::getenv("FA18_HUD_RETICLE_DEBUG")) {
    static int reports = 0;
    if (reports++ == 0)
      std::fprintf(stderr, "HUD shader loc=%d reticle=%d colour=%06x\n",
                   gl.GetUniformLocation(st.present_prog, "u_reticle"), int(hud_state.reticle),
                   hud_state.reticle_rgb);
  }
  gl.Uniform1i(gl.GetUniformLocation(st.present_prog, "u_cockpit"), 6);
  gl.Uniform2f(gl.GetUniformLocation(st.present_prog, "u_dimensions"), float(wide),
               float(layer.scene_rows));
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_side"), float(side));
  gl.Uniform1f(gl.GetUniformLocation(st.present_prog, "u_scale"), float(S));
  static const float quad[12] = {-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
  gl.BindBuffer(GL_ARRAY_BUFFER, st.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
  gl.DisableVertexAttribArray(1);
  gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 0, nullptr);
  gl.EnableVertexAttribArray(0);
  gl.DrawArrays(GL_TRIANGLES, 0, 6);
  if (std::getenv("FA18_GL_DIRECT_DEBUG")) {
    static int errors = 0;
    const GLenum err = gl.GetError();
    if (err || errors++ == 0)
      std::fprintf(stderr, "gl: direct viewport %dx%d at %d,%d drawable %dx%d GL error %#x\n",
                   vw, vh, (drawable_w - vw) / 2, (drawable_h - vh) / 2,
                   drawable_w, drawable_h, err);
  }
  gl.UseProgram(0);
  gl.ActiveTexture(GL_TEXTURE0);
  return true;
}

bool present_bitmap_gpu(const uint32_t* pixels, int width, int height,
                        int logical_width, int drawable_w, int drawable_h,
                        const uint8_t* hud_rgba) {
  if (!ready() || !pixels || width <= 0 || height <= 0 || logical_width <= 0 ||
      drawable_w <= 0 || drawable_h <= 0 || st.bitmap_failed) return false;
  st.make_current();
  if (!st.bitmap_prog) {
    std::string err;
    const GLuint vs = compile(GL_VERTEX_SHADER, kPresentVertex, err);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kBitmapFragment, err);
    if (!vs || !fs) {
      std::fprintf(stderr, "gl: native bitmap presenter unavailable (%s)\n", err.c_str());
      st.bitmap_failed = true;
      return false;
    }
    st.bitmap_prog = gl.CreateProgram();
    gl.AttachShader(st.bitmap_prog, vs);
    gl.AttachShader(st.bitmap_prog, fs);
    gl.BindAttribLocation(st.bitmap_prog, 0, "a_pos");
    gl.LinkProgram(st.bitmap_prog);
    GLint linked = 0;
    gl.GetProgramiv(st.bitmap_prog, GL_LINK_STATUS, &linked);
    if (!linked) {
      char log[2048] = {};
      gl.GetProgramInfoLog(st.bitmap_prog, sizeof(log), nullptr, log);
      std::fprintf(stderr, "gl: native bitmap presenter link: %s\n", log);
      st.bitmap_failed = true;
      return false;
    }
  }

  // Upload only the 2D bitmap, never the old 3D scene. Normal flight uses
  // present_gpu() and keeps its 3D geometry and UI on the GPU throughout.
  static std::vector<uint8_t> rgba;
  rgba.resize(size_t(width) * size_t(height) * 4);
  for (size_t i = 0, n = size_t(width) * size_t(height); i < n; ++i) {
    const uint32_t argb = pixels[i];
    rgba[i * 4 + 0] = uint8_t(argb >> 16);
    rgba[i * 4 + 1] = uint8_t(argb >> 8);
    rgba[i * 4 + 2] = uint8_t(argb);
    rgba[i * 4 + 3] = 255;
  }
  auto upload = [&](GLuint& tex, int unit, int w, int h, const uint8_t* data, bool resize) {
    gl.ActiveTexture(GL_TEXTURE0 + unit);
    if (!tex) gl.GenTextures(1, &tex);
    gl.BindTexture(GL_TEXTURE_2D, tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (resize) gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    else gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
  };
  upload(st.bitmap_tex, 0, width, height, rgba.data(),
         st.bitmap_tex_w != width || st.bitmap_tex_h != height);
  st.bitmap_tex_w = width;
  st.bitmap_tex_h = height;

  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.Viewport(0, 0, drawable_w, drawable_h);
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_DEPTH_TEST);
  gl.Disable(GL_STENCIL_TEST);
  gl.Disable(GL_CULL_FACE);
  gl.Disable(GL_BLEND);
  gl.ColorMask(1, 1, 1, 1);
  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  const double aspect = double(logical_width) / 240.0;
  const int vw = std::min(drawable_w, int(std::round(drawable_h * aspect)));
  const int vh = std::min(drawable_h, int(std::round(drawable_w / aspect)));

  gl.UseProgram(st.bitmap_prog);
  gl.Uniform1i(gl.GetUniformLocation(st.bitmap_prog, "u_image"), 0);
  static const float quad[12] = {-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
  gl.BindBuffer(GL_ARRAY_BUFFER, st.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
  gl.DisableVertexAttribArray(1);
  gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 0, nullptr);
  gl.EnableVertexAttribArray(0);
  gl.Viewport((drawable_w - vw) / 2, (drawable_h - vh) / 2, vw, vh);
  gl.DrawArrays(GL_TRIANGLES, 0, 6);

  if (hud_rgba) {
    // In widescreen, the statistics layer stays at the *window* top-left,
    // rather than the left edge of the centred 4:3 bitmap.
    upload(st.hud_tex, 0, 960, 720, hud_rgba, !st.hud_tex);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.Enable(GL_BLEND);
    gl.Viewport(0, 0, std::min(drawable_w, int(std::round(drawable_h * 4.0 / 3.0))),
                drawable_h);
    gl.DrawArrays(GL_TRIANGLES, 0, 6);
    gl.Disable(GL_BLEND);
  }
  gl.UseProgram(0);
  gl.ActiveTexture(GL_TEXTURE0);
  if (std::getenv("FA18_GL_BITMAP_DEBUG")) {
    static int count = 0;
    if (++count <= 4 || count % 120 == 0)
      std::fprintf(stderr, "gl: native bitmap presentation %d (%dx%d, logical %d, hud %d), error %#x\n",
                   count, width, height, logical_width, hud_rgba != nullptr,
                   gl.GetError());
  }
  return true;
}

bool read_gpu_window(std::vector<uint8_t>& rgba, int w, int h) {
  if (!active() || w <= 0 || h <= 0) return false;
  st.make_current();
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
  rgba.resize(size_t(w) * size_t(h) * 4);
  gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  return true;
}

}  // namespace glr
