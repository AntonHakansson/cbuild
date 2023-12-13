#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"

#include "freetype/freetype.h"

#define HANDMADE_MATH_IMPLEMENTATION
#define HANDMADE_MATH_NO_SSE
#include "HandmadeMath.h"

#include "editor.glsl.h"

#define CBUILD_IMPLEMENTATION
#include "../cbuild.h"

#define CB_RANGE(cb) &(sg_range){ .ptr = (cb).items, .size = (cb).len * sizeof((cb).items[0])}

#define GLYPH_FONT_SIZE 64
#define GLYPH_METRICS_CAPACITY 128

#define MAX_VERTICES 1024
#define MAX_INDICES (4 * 1024)

typedef struct Glyph_Metric Glyph_Metric;
struct Glyph_Metric {
  float ax; // advance.x
  float ay; // advance.y

  float bw; // bitmap.width;
  float bh; // bitmap.rows;

  float bl; // bitmap_left;
  float bt; // bitmap_top;

  float tx; // x offset of glyph in texture coordinates
};

typedef struct Glyph_Atlas Glyph_Atlas;
struct Glyph_Atlas{
  FT_UInt width;
  FT_UInt height;
  sg_image image;
  Glyph_Metric metrics[GLYPH_METRICS_CAPACITY];
};

typedef struct Vertex Vertex;
struct Vertex {
  hmm_v3 pos;
  hmm_v4 col;
  hmm_v2 uv;
};

typedef struct Vertices Vertices;
struct Vertices {
  Vertex *items;
  CB_size capacity;
  CB_size len;
};

typedef struct Indices Indices;
struct Indices {
  uint16_t *items;
  CB_size capacity;
  CB_size len;
};

typedef struct Text_Chunk Text_Chunk;
struct Text_Chunk {
  CB_u8 *items;
  CB_size capacity;
  CB_size len;
};

#define GAP_BUFFER_CHUNK_SIZE (4096)

typedef int Buffer_Position;
typedef struct Gap_Buffer Gap_Buffer;
struct Gap_Buffer {
  CB_u8 buf[GAP_BUFFER_CHUNK_SIZE];
  CB_size buf_capacity;
  Buffer_Position gap_start;
  Buffer_Position gap_end;
  // Gap_Buffer *next; // TODO: Maybe chain
};

Gap_Buffer *new_gap_buffer(CB_Arena *arena)
{
  Gap_Buffer *gb = new(arena, Gap_Buffer, 1);
  gb->buf_capacity = GAP_BUFFER_CHUNK_SIZE;
  gb->gap_end = GAP_BUFFER_CHUNK_SIZE;
  return gb;
}

void gb_shift_gap_to(Gap_Buffer *gb, Buffer_Position cursor)
{
  CB_size gap_len = gb->gap_end - gb->gap_start;
  CB_assert(gap_len >= 0);
  cursor = CB_min(cursor, gb->buf_capacity - gap_len);
  if (cursor == gb->gap_start) { return; }

  if (gb->gap_start < cursor) {
    // buf:    12345....6789abdc
    // cursor: .......|.........
    // buf':   1234567....89abdc
    CB_usize delta = cursor - gb->gap_start;
    CB_memcpy(&gb->buf[gb->gap_start], &gb->buf[gb->gap_end], delta);
    gb->gap_start += delta;
    gb->gap_end   += delta;
  }
  else if (gb->gap_start > cursor) {
    // buf:    12345....6789abdc
    // cursor: ..|...............
    // buf':   12....3456789abdc
    CB_usize delta = gb->gap_start - cursor;
    CB_memcpy(&gb->buf[gb->gap_end - delta], &gb->buf[gb->gap_start - delta], delta);
    gb->gap_start -= delta;
    gb->gap_end   -= delta;
  }
}

void gb_insert_char(Gap_Buffer *gb, Buffer_Position cursor, CB_u8 c)
{
  // TODO check bounds
  gb_shift_gap_to(gb, cursor);
  gb->buf[gb->gap_start] = c;
  gb->gap_start++;
}

void gb_delete(Gap_Buffer *gb, Buffer_Position cursor, CB_size n_bytes)
{
  // TODO check bounds
  CB_assert(n_bytes > 0);
  gb_shift_gap_to(gb, cursor);
  gb->gap_start = CB_max(gb->gap_start - n_bytes, 0);
  CB_memset(&gb->buf[gb->gap_start], 0, n_bytes);
}

typedef struct Gap_Buffer_Result Gap_Buffer_Result;
struct Gap_Buffer_Result {
  CB_Str left;
  CB_Str right;
};

Gap_Buffer_Result gb_get_strings(Gap_Buffer *gb)
{
  Gap_Buffer_Result result = {0};
  result.left.buf = gb->buf;
  result.left.len = gb->gap_start;
  result.right.buf = gb->buf + gb->gap_end;
  result.right.len = gb->buf_capacity - gb->gap_end;
  return result;
}

enum {
  EDITOR_LINES_DIRTY = 1 << 1,
};

typedef struct Editor Editor;
struct Editor {
  CB_u32 flags;
  Buffer_Position cursor;

  Gap_Buffer *gap_buffer;

  CB_Str  lines[GAP_BUFFER_CHUNK_SIZE];
  CB_size lines_len;
};

Editor *new_editor(CB_Arena *arena)
{
  Editor *result     = new(arena, Editor, 1);
  result->gap_buffer = new_gap_buffer(arena);
  return result;
}

void editor_recalc_line_(Editor *ed, CB_Str str)
{
  CB_size line_start_idx = 0;
  CB_size line_one_past_end_idx = 0;

  for (CB_size i = 0; i < str.len; i++) {
    CB_Str *line = &ed->lines[ed->lines_len];
    CB_u8 c = str.buf[i];

    if (c == '\n') {
      line_one_past_end_idx = i;
      line_start_idx = i + 1;
      ed->lines_len++;
    }
    else {
      line_one_past_end_idx = i + 1;

      line->buf = str.buf + line_start_idx;
      line->len = line_one_past_end_idx - line_start_idx;
    }
  }
}

void editor_recalc_lines(Editor *ed)
{
  if (!(ed->flags & EDITOR_LINES_DIRTY)) return;

  ed->lines_len = 0;

  Gap_Buffer_Result halfs = gb_get_strings(ed->gap_buffer);
  editor_recalc_line_(ed, halfs.left);
  editor_recalc_line_(ed, halfs.right);

  ed->flags &= ~(EDITOR_LINES_DIRTY);
}

void editor_event(Editor *ed, const sapp_event* e)
{
  if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
    if (e->key_code == SAPP_KEYCODE_LEFT) {
      ed->cursor--;
    }
    if (e->key_code == SAPP_KEYCODE_RIGHT) {
      ed->cursor++;
    }

    if (e->key_code == SAPP_KEYCODE_BACKSPACE) {
      gb_delete(ed->gap_buffer, ed->cursor, 1);
      ed->cursor--;
      ed->flags |= EDITOR_LINES_DIRTY;
    }

    if (e->key_code == SAPP_KEYCODE_ENTER) {
      gb_insert_char(ed->gap_buffer, ed->cursor, '\n');
      ed->flags |= EDITOR_LINES_DIRTY;
      ed->cursor += 1;
    }
  }

  if (e->type == SAPP_EVENTTYPE_CHAR) {
    if (e->char_code >= 32 && e->char_code < 128) {
      gb_insert_char(ed->gap_buffer, ed->cursor, e->char_code);
      ed->flags |= EDITOR_LINES_DIRTY;
      ed->cursor += 1;
    }
  }

  ed->cursor = CB_clamp(ed->cursor, 0, ed->gap_buffer->buf_capacity - 1);
}

static struct {
  sg_pipeline pip;
  sg_bindings bind;
  sg_pass_action pass_action;

  float elapsed_time;

  CB_Arena *perm_arena;
  CB_Arena *frame_arena;

  Editor *editor;

  sg_buffer vbuf;
  sg_buffer ibuf;
  Glyph_Atlas glyph_atlas;
  FT_Library library;
  FT_Face face;
} state;


static void init(void)
{
  state.perm_arena = cb_alloc_arena(8 * 1024 * 1024);
  state.frame_arena = cb_alloc_arena(512 * 1024 * 1024);

  state.editor = new_editor(state.perm_arena);

  sg_setup(&(sg_desc){
    .context = sapp_sgcontext(),
    .logger.func = slog_func,
  });

  { // Freetype
  FT_Error error = FT_Init_FreeType(&state.library);
  if (error) {
    fprintf(stderr, "ERROR: could initialize FreeType2 library\n");
    sapp_request_quit();
  }

  const char *const font_file_path = "./iosevka-regular.ttf";

  error = FT_New_Face(state.library, font_file_path, 0, &state.face);
  if (error == FT_Err_Unknown_File_Format) {
    fprintf(stderr, "ERROR: `%s` has an unknown format\n", font_file_path);
    sapp_request_quit();
  } else if (error) {
    fprintf(stderr, "ERROR: could not load file `%s`\n", font_file_path);
    sapp_request_quit();
  }

  FT_UInt pixel_size = GLYPH_FONT_SIZE;
  error = FT_Set_Pixel_Sizes(state.face, 0, pixel_size);
  if (error) {
    fprintf(stderr, "ERROR: could not set pixel size to %u\n", pixel_size);
    sapp_request_quit();
  }

  // Build ASCII Glyph atlas
  Glyph_Atlas *atlas = &state.glyph_atlas;
  FT_Face face = state.face;
  FT_Int32 load_flags = FT_LOAD_RENDER | FT_LOAD_TARGET_(FT_RENDER_MODE_SDF);

  const int start_i = 32;
  const int end_i = 128;
  for (int i = start_i; i < end_i; i++) {
    if (FT_Load_Char(face, i, load_flags)) {
      fprintf(stderr, "ERROR: could not load glyph of a character with code %d\n", i);
      exit(1);
    }
    atlas->width += face->glyph->bitmap.width;
    if (atlas->height < face->glyph->bitmap.rows) {
      atlas->height = face->glyph->bitmap.rows;
    }
  }

  uint8_t *pixels = new(state.perm_arena, uint8_t, atlas->width * atlas->height);
  int x = 0;

  for (int i = start_i; i < end_i; i++) {
    if (FT_Load_Char(face, i, load_flags)) {
      fprintf(stderr, "ERROR: could not load glyph of a character with code %d\n", i);
      exit(1);
    }

    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
      fprintf(stderr, "ERROR: could not render glyph of a character with code %d\n", i);
      exit(1);
    }

    atlas->metrics[i].ax = face->glyph->advance.x >> 6;
    atlas->metrics[i].ay = face->glyph->advance.y >> 6;
    atlas->metrics[i].bw = face->glyph->bitmap.width;
    atlas->metrics[i].bh = face->glyph->bitmap.rows;
    atlas->metrics[i].bl = face->glyph->bitmap_left;
    atlas->metrics[i].bt = face->glyph->bitmap_top;
    atlas->metrics[i].tx = (float) x / (float) atlas->width;

    // @speed: blit using GPU, SIMD
    for (CB_size row = 0; row < face->glyph->bitmap.rows; row++) {
      for (CB_size col = 0; col < face->glyph->bitmap.width; col++) {
        CB_size glyph_pitch = face->glyph->bitmap.width;
        uint8_t p = *((uint8_t *)face->glyph->bitmap.buffer + row * glyph_pitch + col);
        pixels[row * atlas->width + x + col] = p;
      }
    }
    x += face->glyph->bitmap.width;
  }

  atlas->image = sg_make_image(&(sg_image_desc){
    .width = atlas->width,
    .height = atlas->height,
    .usage = SG_USAGE_DYNAMIC,
    .pixel_format = SG_PIXELFORMAT_R8,
    .label = "font-atlas",
  });
  sg_update_image(state.glyph_atlas.image, &(sg_image_data){
    .subimage[0][0] = (sg_range){
      .ptr = pixels,
      .size = atlas->width * atlas->height * sizeof(pixels[0]),
    }});
  }

  sg_sampler smp = sg_make_sampler(&(sg_sampler_desc){
    .min_filter = SG_FILTER_NEAREST,
    .mag_filter = SG_FILTER_NEAREST,
    .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
    .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
  });

  state.vbuf = sg_make_buffer(&(sg_buffer_desc){
    .type = SG_BUFFERTYPE_VERTEXBUFFER,
    .size = MAX_VERTICES * sizeof(Vertex),
    .usage = SG_USAGE_STREAM,
    .label = "ui-vertices"
  });

  state.ibuf = sg_make_buffer(&(sg_buffer_desc){
    .type = SG_BUFFERTYPE_INDEXBUFFER,
    .size = MAX_INDICES * sizeof(uint16_t),
    .usage = SG_USAGE_STREAM,
    .label = "ui-indices"
  });

  sg_shader shd = sg_make_shader(editor_shader_desc(sg_query_backend()));
  state.pip = sg_make_pipeline(&(sg_pipeline_desc){
    .shader = shd,
    .layout = {
      .attrs = {
        [ATTR_vs_position0].format = SG_VERTEXFORMAT_FLOAT3,
        [ATTR_vs_color0].format    = SG_VERTEXFORMAT_FLOAT4,
        [ATTR_vs_texcoord0].format = SG_VERTEXFORMAT_FLOAT2
      }
    },
    .index_type = SG_INDEXTYPE_UINT16,
    .cull_mode  = SG_CULLMODE_BACK,
    .colors[0].blend = {
      .enabled = true,
      .src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA,
      .dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    },
    .label = "ui-pipeline",
  });

  state.pass_action = (sg_pass_action) {
    .colors[0] = { .load_action=SG_LOADACTION_CLEAR, .clear_value={0.0f, 0.0f, 0.0f, 1.0f } },
  };

  state.bind = (sg_bindings) {
    .vertex_buffers[0] = state.vbuf,
    .index_buffer      = state.ibuf,
    .fs = { .images[SLOT_tex] = state.glyph_atlas.image, .samplers[SLOT_smp] = smp },
  };
}

void push_vertex(Vertices *verts, hmm_v3 pos, hmm_v4 color, hmm_v2 uv)
{
  CB_assert(verts->len < verts->capacity);
  Vertex v = {0};
  v.pos    = pos;
  v.col  = color;
  v.uv     = uv;

  *cb_da_push_unsafe(verts) = v;
}

void push_image_rect(Indices *indices, Vertices *verts, hmm_v2 pos, hmm_v2 dim, hmm_v2 uv_pos, hmm_v2 uv_dim, hmm_v4 col)
{
  CB_assert(indices->len < indices->capacity);
  hmm_v3 p0 = HMM_Vec3(pos.X + 0.0f,  pos.Y + 0.0f,  0.f);
  hmm_v3 p1 = HMM_Vec3(pos.X + dim.X, pos.Y + 0.0f,  0.f);
  hmm_v3 p2 = HMM_Vec3(pos.X + dim.X, pos.Y + dim.Y, 0.f);
  hmm_v3 p3 = HMM_Vec3(pos.X + 0.f,   pos.Y + dim.Y, 0.f);

  hmm_v2 uv0 = HMM_Vec2(uv_pos.X + 0.f,      uv_pos.Y + 0.f);
  hmm_v2 uv1 = HMM_Vec2(uv_pos.X + uv_dim.X, uv_pos.Y + 0.f);
  hmm_v2 uv2 = HMM_Vec2(uv_pos.X + uv_dim.X, uv_pos.Y + uv_dim.Y);
  hmm_v2 uv3 = HMM_Vec2(uv_pos.X + 0.f,      uv_pos.Y + uv_dim.Y);

  CB_size i = verts->len;
  push_vertex(verts, p0, col, uv0);
  push_vertex(verts, p1, col, uv1);
  push_vertex(verts, p2, col, uv2);
  push_vertex(verts, p3, col, uv3);

  *(cb_da_push_unsafe(indices)) = i + 0;
  *(cb_da_push_unsafe(indices)) = i + 1;
  *(cb_da_push_unsafe(indices)) = i + 2;
  *(cb_da_push_unsafe(indices)) = i + 3;
  *(cb_da_push_unsafe(indices)) = i + 0;
  *(cb_da_push_unsafe(indices)) = i + 2;
}

void push_rect(Indices *indices, Vertices *verts, hmm_v2 pos, hmm_v2 dim, hmm_v4 col)
{
  CB_assert(indices->len < indices->capacity);

  hmm_v3 p0 = HMM_Vec3(pos.X + 0.0f,  pos.Y + 0.0f,  0.f);
  hmm_v3 p1 = HMM_Vec3(pos.X + dim.X, pos.Y + 0.0f,  0.f);
  hmm_v3 p2 = HMM_Vec3(pos.X + dim.X, pos.Y + dim.Y, 0.f);
  hmm_v3 p3 = HMM_Vec3(pos.X + 0.f,   pos.Y + dim.Y, 0.f);

  hmm_v2 uv0 = HMM_Vec2(0.f, 0.f);
  hmm_v2 uv1 = HMM_Vec2(1.f, 0.f);
  hmm_v2 uv2 = HMM_Vec2(1.f, 1.f);
  hmm_v2 uv3 = HMM_Vec2(0.f, 1.f);

  CB_size i = verts->len;
  push_vertex(verts, p0, col, uv0);
  push_vertex(verts, p1, col, uv1);
  push_vertex(verts, p2, col, uv2);
  push_vertex(verts, p3, col, uv3);

  *(cb_da_push_unsafe(indices)) = i + 0;
  *(cb_da_push_unsafe(indices)) = i + 1;
  *(cb_da_push_unsafe(indices)) = i + 2;
  *(cb_da_push_unsafe(indices)) = i + 3;
  *(cb_da_push_unsafe(indices)) = i + 0;
  *(cb_da_push_unsafe(indices)) = i + 2;
}

void push_text_line(Indices *indices, Vertices *verts, Glyph_Atlas *atlas, hmm_vec2 *pos, CB_Str text, hmm_v4 color)
{
  for (CB_size i = 0; i < text.len; i++) {
    CB_usize glyph_index = text.buf[i];
    // TODO support glyphs outside ascii range
    if (glyph_index < 32 || glyph_index >= GLYPH_METRICS_CAPACITY) {
      glyph_index = '?';
    }
    Glyph_Metric metric = atlas->metrics[glyph_index];
    float x2 =  pos->X + metric.bl;
    float y2 = -pos->Y - metric.bt;
    float w  = metric.bw;
    float h  = metric.bh;

    pos->X += metric.ax;
    pos->Y += metric.ay;

    push_image_rect(indices, verts,
                    HMM_Vec2(x2, -y2), HMM_Vec2(w, -h),
                    HMM_Vec2(metric.tx, 0.0f),
                    HMM_Vec2(metric.bw / (float)atlas->width, metric.bh  / (float)atlas->height),
                    color);
  }
}

void frame(void)
{
  const float w = sapp_widthf();
  const float h = sapp_heightf();
  const float aspect = w / h;
  const float t = (float)(sapp_frame_duration() * 60.0);
  state.elapsed_time += t;

  vs_params_t vs_params = {0};
  hmm_mat4 proj = HMM_Orthographic(0.f, 1080.f, 0.f, 1080.f / aspect, 0.f, 10.0f);
  vs_params.mvp = proj;

  hmm_v4 white = HMM_Vec4(1.f, 1.f, 1.f, 1.f);
  Vertices verts  = cb_da_init(state.frame_arena, Vertices, 1024);
  Indices indices = cb_da_init(state.frame_arena, Indices, 4096);

  Glyph_Atlas *atlas = &state.glyph_atlas;

  editor_recalc_lines(state.editor);

  hmm_vec2 text_pos = HMM_Vec2(5.f, 750.f);
  for (CB_size i = 0; i <= state.editor->lines_len; i++) {
    text_pos.X  = 5.f;
    push_text_line(&indices, &verts, atlas, &text_pos, state.editor->lines[i], white);
    text_pos.Y -= 100.f;
  }

  /* Gap_Buffer_Result halfs = gb_get_strings(state.editor->gap_buffer); */
  /* push_text_line(&indices, &verts, atlas, &text_pos, halfs.left, white); */
  /* push_text_line(&indices, &verts, atlas, &text_pos, halfs.right, white); */
  /* push_rect(&indices, &verts, HMM_Vec2(state.editor->cursor, 0), HMM_Vec2(100.f, 500.f), white); */

  if (indices.len > 0) {
    sg_update_buffer(state.vbuf, CB_RANGE(verts));
    sg_update_buffer(state.ibuf, CB_RANGE(indices));
  }

  sg_begin_default_pass(&state.pass_action, sapp_width(), sapp_height());
  sg_apply_pipeline(state.pip);
  sg_apply_bindings(&state.bind);
  sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_vs_params, &SG_RANGE(vs_params));
  sg_draw(0, indices.len, 1);
  sg_end_pass();
  sg_commit();

  cb_arena_reset(state.frame_arena);
}

void event(const sapp_event* e)
{
  if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
    if (e->key_code == SAPP_KEYCODE_ESCAPE) {
      sapp_request_quit();
    }
  }

  editor_event(state.editor, e);
}

void cleanup(void)
{
  sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[])
{
  (void)argc; (void)argv;
  return (sapp_desc){
    .init_cb = init,
    .frame_cb = frame,
    .event_cb = event,
    .cleanup_cb = cleanup,
    .width = 640,
    .height = 480,
    .window_title = "Editor.c",
    .icon.sokol_default = true,
    .logger.func = slog_func,
  };
}
