// Nate - not a text editor

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

#define GLYPH_FONT_SIZE (64)
#define GLYPH_METRICS_CAPACITY 128

#define MAX_VERTICES 4 * 1024
#define MAX_INDICES (3 * MAX_VERTICES)

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
struct Glyph_Atlas {
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


#define TEXT_CHUNK_SIZE (10 * 1024)

typedef struct Text_Chunk Text_Chunk;
struct Text_Chunk {
  CB_u8 buf[TEXT_CHUNK_SIZE];
  CB_size len;
  //  Text_Chunk *next;
};

enum {
  EDITOR_LINES_DIRTY = 1 << 0,
};

typedef int Buffer_Position;

typedef struct Line Line;
struct Line {
  Buffer_Position start;
  Buffer_Position end;
};

typedef struct Editor Editor;
struct Editor {
  CB_u32 flags;
  Buffer_Position cursor;
  Text_Chunk *text_buffer;
  Line lines[TEXT_CHUNK_SIZE];
  CB_size lines_len;

  hmm_vec2 camera_pos;
  hmm_vec2 target_camera_pos;
  float camera_zoom;
  float target_camera_zoom;
};

static Editor *new_editor(CB_Arena *arena)
{
  Editor *result     = new(arena, Editor, 1);
  result->text_buffer = new(arena, Text_Chunk, 1);
  result->flags |= EDITOR_LINES_DIRTY;
  result->camera_zoom = 1.f;
  return result;
}

static void editor_recalc_lines(Editor *ed)
{
  if (!(ed->flags & EDITOR_LINES_DIRTY)) return;

  CB_memset(ed->lines, 0, ed->lines_len * sizeof(ed->lines[0]));
  ed->lines_len = 0;

  {
    Line line = {0};
    for (CB_size i = 0; i < ed->text_buffer->len; i++) {
      if (ed->text_buffer->buf[i] == '\n') {
        line.end = i;
        ed->lines[ed->lines_len++] = line;
        line.start = i + 1;
      }
    }
    line.end = ed->text_buffer->len;
    ed->lines[ed->lines_len++] = line;
  }

  ed->flags &= ~(EDITOR_LINES_DIRTY);
}

static void editor_ins(Editor *ed, char c)
{
  CB_assert(ed->text_buffer->len < TEXT_CHUNK_SIZE);
  ed->flags |= EDITOR_LINES_DIRTY;

  CB_u8 *buf =  ed->text_buffer->buf;
  CB_size len =  ed->text_buffer->len;
  Buffer_Position cursor = ed->cursor;

  memmove(buf + cursor + 1, buf + cursor, len - cursor);

  ed->text_buffer->buf[cursor] = c;
  ed->text_buffer->len++;
  ed->cursor += 1;
}

static void editor_bspc(Editor *ed)
{
  if (ed->cursor <= 0) return;

  ed->flags |= EDITOR_LINES_DIRTY;

  CB_u8 *buf =  ed->text_buffer->buf;
  CB_size len =  ed->text_buffer->len;
  Buffer_Position cursor = ed->cursor;

  int amount = 1;
  memmove(buf + cursor - amount, buf + cursor, len - cursor);
  ed->text_buffer->len -= amount;
  ed->cursor -= amount;
}

static void editor_event(Editor *ed, const sapp_event* e)
{
  if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
    if (e->key_code == SAPP_KEYCODE_LEFT) {
      ed->cursor--;
    }
    if (e->key_code == SAPP_KEYCODE_RIGHT) {
      ed->cursor++;
    }

    if (e->key_code == SAPP_KEYCODE_BACKSPACE) {
      editor_bspc(ed);
    }

    if (e->key_code == SAPP_KEYCODE_ENTER) {
      editor_ins(ed, '\n');
    }
  }

  if (e->type == SAPP_EVENTTYPE_CHAR) {
    if (e->char_code >= 32 && e->char_code < 128) {
      editor_ins(ed, e->char_code);
    }
  }

  ed->cursor = CB_clamp(ed->cursor, 0, ed->text_buffer->len);
}

static void push_vertex(Vertices *verts, hmm_v3 pos, hmm_v4 color, hmm_v2 uv)
{
  CB_assert(verts->len < verts->capacity);
  Vertex v = {0};
  v.pos    = pos;
  v.col  = color;
  v.uv     = uv;

  *cb_da_push_unsafe(verts) = v;
}

static void push_image_rect(Indices *indices, Vertices *verts, hmm_v2 pos, hmm_v2 dim, hmm_v2 uv_pos, hmm_v2 uv_dim, hmm_v4 col)
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

static void push_rect(Indices *indices, Vertices *verts, hmm_v2 pos, hmm_v2 dim, hmm_v4 col)
{
  CB_assert(indices->len < indices->capacity);

  hmm_v3 p0 = HMM_Vec3(pos.X + 0.0f,  pos.Y + 0.0f,  0.f);
  hmm_v3 p1 = HMM_Vec3(pos.X + dim.X, pos.Y + 0.0f,  0.f);
  hmm_v3 p2 = HMM_Vec3(pos.X + dim.X, pos.Y + dim.Y, 0.f);
  hmm_v3 p3 = HMM_Vec3(pos.X + 0.f,   pos.Y + dim.Y, 0.f);

  // Special white texture region
  hmm_v2 uv0 = HMM_Vec2(1.f, 0.f);
  hmm_v2 uv1 = HMM_Vec2(1.f, 0.f);
  hmm_v2 uv2 = HMM_Vec2(1.f, 0.f);
  hmm_v2 uv3 = HMM_Vec2(1.f, 0.f);

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

typedef struct Text_Bounding_Box Text_Bounding_Box;
struct Text_Bounding_Box {
  hmm_vec2 bottom_left;
  hmm_vec2 top_right;
};

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

    if (indices && verts) {
      push_image_rect(indices, verts,
                      HMM_Vec2(x2, -y2), HMM_Vec2(w, -h),
                      HMM_Vec2(metric.tx, 0.0f),
                      HMM_Vec2(metric.bw / (float)atlas->width, metric.bh  / (float)atlas->height),
                      color);
    }
  }
}

static struct {
  sg_pipeline pip;
  sg_bindings bind;
  sg_pass_action pass_action;

  float elapsed_time;

  CB_Arena *perm_arena;
  CB_Arena *frame_arena;

  Editor *editor;

  fs_params_t fs_params;
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

  const char *const font_file_path = "./vendor/IosevkaTermNerdFont-Regular.ttf";

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

  CB_i32 white_texture_dim = 64;
  atlas->width += white_texture_dim;

  uint8_t *pixels = new(state.frame_arena, uint8_t, atlas->width * atlas->height);

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

  // Blit white texture at the end
  // @speed: blit using GPU, SIMD
  for (CB_size row = 0; row < white_texture_dim; row++) {
    for (CB_size col = 0; col < white_texture_dim; col++) {
      int idx = row * atlas->width + x + col;
      uint8_t p = 0xFF; // white!!!
      pixels[idx] = p;
    }
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
    .min_filter = SG_FILTER_LINEAR,
    .mag_filter = SG_FILTER_LINEAR,
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

static void frame(void)
{
  const float w = sapp_widthf();
  const float h = sapp_heightf();
  const float t = (float)(sapp_frame_duration() * 60.0);
  state.elapsed_time += t;


  hmm_v4   white = HMM_Vec4(1.f, 1.f, 1.f, 1.f);
  Vertices verts  = cb_da_init(state.frame_arena, Vertices, 1024);
  Indices  indices = cb_da_init(state.frame_arena, Indices, 4096);


  { // Render Editor
    editor_recalc_lines(state.editor);

    Glyph_Atlas *atlas = &state.glyph_atlas;
    hmm_vec2 text_pos = HMM_Vec2(5.f, 0);
    hmm_vec2 cursor_pos = {0};
    float max_width = 0;
    for (CB_size i = 0; i < state.editor->lines_len; i++) {
      text_pos.X  = 5.f;

      Line line = state.editor->lines[i];
      CB_Str str = (CB_Str){ .buf = &state.editor->text_buffer->buf[line.start], .len = line.end - line.start, };

      // render cursor
      Buffer_Position cursor = state.editor->cursor;
      CB_b32 cursor_visible = ((int)(state.elapsed_time * 0.025f)) % 2 == 0;
      if (line.start <= cursor && cursor <= line.end) {
        CB_Str   str_up_to_cursor = (CB_Str){ .buf = &state.editor->text_buffer->buf[line.start], .len = cursor - line.start, };
        cursor_pos = text_pos;
        push_text_line(0, 0, atlas, &cursor_pos, str_up_to_cursor, white); // compute cursor pos

        if (cursor_visible) {
          push_rect(&indices, &verts,
                    HMM_Vec2(cursor_pos.X, cursor_pos.Y + GLYPH_FONT_SIZE - 10.f), HMM_Vec2(10, -GLYPH_FONT_SIZE),
                    white);
        }
      }

      // render text
      push_text_line(&indices, &verts, atlas, &text_pos, str, white);

      max_width = CB_max(max_width, text_pos.X);
      text_pos.Y -= state.glyph_atlas.height;
    }

    // interpolate editor camera
    {
      state.editor->target_camera_pos = HMM_Vec2(0.f, cursor_pos.Y);
      state.editor->camera_pos
        = HMM_AddVec2(state.editor->camera_pos,
                      HMM_MultiplyVec2f(HMM_SubtractVec2(state.editor->target_camera_pos,
                                                         state.editor->camera_pos),
                                        0.88f));

      state.editor->target_camera_zoom = CB_clamp((w / (max_width + 10.f)), 0.3f, 1.3f);
      state.editor->camera_zoom += (state.editor->target_camera_zoom - state.editor->camera_zoom) * 0.88f;
    }
  }

  if (indices.len > 0) {
    sg_update_buffer(state.vbuf, CB_RANGE(verts));
    sg_update_buffer(state.ibuf, CB_RANGE(indices));
  }

  // Update Camera Transform
  hmm_mat4 proj = HMM_Orthographic(0.f, w, 0.f, h, 0.f, 10.0f);
  hmm_mat4 centered_cam_in_y =
    HMM_MultiplyMat4(HMM_Translate(HMM_Vec3(0.f, h * .5f, 0.f)),
                     HMM_Scale(HMM_Vec3(state.editor->camera_zoom, state.editor->camera_zoom, 1.0f)));
  hmm_mat4 view = HMM_MultiplyMat4(centered_cam_in_y,
                                   HMM_Translate(HMM_Vec3(-state.editor->camera_pos.X, -state.editor->camera_pos.Y, 0.f)));
  vs_params_t vs_params = { .mvp = HMM_MultiplyMat4(proj, view), };

  sg_begin_default_pass(&state.pass_action, sapp_width(), sapp_height());
  sg_apply_pipeline(state.pip);
  sg_apply_bindings(&state.bind);
  sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_vs_params, &SG_RANGE(vs_params));
  sg_apply_uniforms(SG_SHADERSTAGE_FS, SLOT_fs_params, &SG_RANGE(state.fs_params));
  sg_draw(0, indices.len, 1);
  sg_end_pass();
  sg_commit();

  cb_arena_reset(state.frame_arena);
}

static void event(const sapp_event* e)
{
  if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
    if (e->key_code == SAPP_KEYCODE_ESCAPE) {
      sapp_request_quit();
    }

    int delta = 0;
    if (e->key_code == SAPP_KEYCODE_PAGE_DOWN) { delta = 1; }
    else if(e->key_code == SAPP_KEYCODE_PAGE_UP) { delta = -1;}
    state.fs_params.effect = ((unsigned int)(state.fs_params.effect + delta)) % 10;

    float smoothness = 0;
    if (e->key_code == SAPP_KEYCODE_HOME) { smoothness = .005f; }
    else if(e->key_code == SAPP_KEYCODE_END) { smoothness = -.005f;}
    state.fs_params.smoothness += smoothness;
  }

  editor_event(state.editor, e);
}

static void cleanup(void)
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
