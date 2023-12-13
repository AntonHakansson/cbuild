// This is free and unencumbered software released into the public domain.

#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#ifdef CBUILD_CONFIGURED
#include "build/config.h"
#endif

CB_b32 build_freetype_library(CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  CB_Write_Buffer *b = cb_mem_buffer(scratch.arena, 4 * 1024);

  CB_Str freetype_out = S("build/libfreetype.a");
  #define FREETYPE_LOC "vendor/freetype/"
  CB_Str_List freetype_sources = cb_str_dup_list(scratch.arena,
    FREETYPE_LOC "src/autofit/autofit.c",
    FREETYPE_LOC "src/base/ftbase.c",
    FREETYPE_LOC "src/base/ftsystem.c",
    FREETYPE_LOC "src/base/ftdebug.c",
    FREETYPE_LOC "src/base/ftbbox.c",
    FREETYPE_LOC "src/base/ftbdf.c",
    FREETYPE_LOC "src/base/ftbitmap.c",
    FREETYPE_LOC "src/base/ftcid.c",
    FREETYPE_LOC "src/base/ftfstype.c",
    FREETYPE_LOC "src/base/ftgasp.c",
    FREETYPE_LOC "src/base/ftglyph.c",
    FREETYPE_LOC "src/base/ftgxval.c",
    FREETYPE_LOC "src/base/ftinit.c",
    FREETYPE_LOC "src/base/ftmm.c",
    FREETYPE_LOC "src/base/ftotval.c",
    FREETYPE_LOC "src/base/ftpatent.c",
    FREETYPE_LOC "src/base/ftpfr.c",
    FREETYPE_LOC "src/base/ftstroke.c",
    FREETYPE_LOC "src/base/ftsynth.c",
    FREETYPE_LOC "src/base/fttype1.c",
    FREETYPE_LOC "src/base/ftwinfnt.c",
    FREETYPE_LOC "src/bdf/bdf.c",
    FREETYPE_LOC "src/bzip2/ftbzip2.c",
    FREETYPE_LOC "src/cache/ftcache.c",
    FREETYPE_LOC "src/cff/cff.c",
    FREETYPE_LOC "src/cid/type1cid.c",
    FREETYPE_LOC "src/gzip/ftgzip.c",
    FREETYPE_LOC "src/lzw/ftlzw.c",
    FREETYPE_LOC "src/pcf/pcf.c",
    FREETYPE_LOC "src/pfr/pfr.c",
    FREETYPE_LOC "src/psaux/psaux.c",
    FREETYPE_LOC "src/pshinter/pshinter.c",
    FREETYPE_LOC "src/psnames/psnames.c",
    FREETYPE_LOC "src/raster/raster.c",
    FREETYPE_LOC "src/sdf/sdf.c",
    FREETYPE_LOC "src/sfnt/sfnt.c",
    FREETYPE_LOC "src/smooth/smooth.c",
    FREETYPE_LOC "src/svg/svg.c",
    FREETYPE_LOC "src/truetype/truetype.c",
    FREETYPE_LOC "src/type1/type1.c",
    FREETYPE_LOC "src/type42/type42.c",
    FREETYPE_LOC "src/winfonts/winfnt.c");

  CB_Str_List obj_files = cb_da_init(scratch.arena, CB_Str_List, 128);
  { // freetype/**/*/<base>.c -> build/freetype/<base>.o
    for (CB_size i = 0; i < freetype_sources.len; i++) {
      CB_Str base = cb_str_chop_right(freetype_sources.items[i], '.');
      // TODO: Implement common Str manipulation operations
      CB_Str left = cb_str_chop_right(base, '/');
      base.len = base.len - left.len - 1;
      base.buf = base.buf + left.len + 1;
      CB_Str_Mark mark = cb_write_buffer_mark(b);
      cb_append_strs(b, S("build/freetype/"), base, S(".o"));
      CB_Str obj = cb_str_from_mark(&mark);
      *(cb_da_push(scratch.arena, &obj_files)) = obj;
    }
  }

  int status = cb_needs_rebuild(freetype_out, freetype_sources.items, freetype_sources.len, stderr);
  if (status <  0) { cb_return_defer(0); }
  if (status == 0) { cb_return_defer(1); }
  if (status >  0) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Building Freetype2 lib ... "));
    if (!cb_mkdir_if_not_exists(S("build/freetype"), stderr)) cb_return_defer(0);

    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 128);
    CB_Procs procs = cb_da_init(scratch.arena, CB_Procs, 128);

    for (CB_size i = 0; i < freetype_sources.len; i++) {
      cmd.len = 0;

      cb_cmd_append_lits(scratch.arena, &cmd, "cc");
      cb_cmd_append_strs(scratch.arena, &cmd, S("-o"), obj_files.items[i]);
      cb_cmd_append_strs(scratch.arena, &cmd, S("-c"), freetype_sources.items[i]);
      cb_cmd_append_lits(scratch.arena, &cmd, "-I" FREETYPE_LOC "include");
      cb_cmd_append_lits(scratch.arena, &cmd, "-DFT2_BUILD_LIBRARY");
      cb_cmd_append_lits(scratch.arena, &cmd, "-DHAVE_UNISTD_H");

      *(cb_da_push(scratch.arena, &procs)) = cb_cmd_run_async(cmd, stderr);
    }

    for (CB_size i = 0; i < procs.len; i++) {
      if (!cb_proc_wait(procs.items[i], stderr)) { continue; }
    }

    cmd.len = 0;
    cb_cmd_append_strs(scratch.arena, &cmd, S("ar"), S("-r"), freetype_out);
    for (CB_size i = 0; i < obj_files.len; i++) {
      cb_cmd_append(scratch.arena, &cmd, obj_files.items[i]);
    }
    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
    cb_return_defer(1);
  }

  CB_assert(0 && "unreachable");
 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

void cmd_freetype_flags(CB_Arena *arena, CB_Command *cmd)
{
  cb_cmd_append_lits(arena, cmd, "-I./vendor/freetype/include/", "-Lbuild/", "-lfreetype");
}

CB_b32 build_sokol_library(CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  CB_Str sokol_out = S("build/libsokol.a");
  #define SOKOL_LOC "vendor/sokol/"
  CB_Str sokol_sources[] = {
    S("src/sokol.c"),
    S(SOKOL_LOC "sokol_app.h"),
    S(SOKOL_LOC "sokol_gfx.h"),
    S(SOKOL_LOC "sokol_time.h"),
    S(SOKOL_LOC "sokol_fetch.h"),
    S(SOKOL_LOC "sokol_log.h"),
    S(SOKOL_LOC "sokol_glue.h"),
  };
  int status = cb_needs_rebuild(sokol_out, sokol_sources, CB_countof(sokol_sources), stderr);
  if (status <  0) { cb_return_defer(0); }
  if (status == 0) { cb_return_defer(1); }
  if (status >  0) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Building Sokol Library ..."));
    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    cb_cmd_append_lits(scratch.arena, &cmd, "cc");
    cb_cmd_append_strs(scratch.arena, &cmd, S("-o"), sokol_out);
    cb_cmd_append_strs(scratch.arena, &cmd, S("-c"), S("src/sokol.c"));
    cb_cmd_append_lits(scratch.arena, &cmd, "-I" SOKOL_LOC);
    cb_cmd_append_lits(scratch.arena, &cmd, "-DSOKOL_GLCORE33");
#if SOKOL_DEBUG
    cb_cmd_append_lits(scratch.arena, &cmd, "-g");
#else
    cb_cmd_append_lits(scratch.arena, &cmd, "-O2");
#endif

    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
    cb_return_defer(1);
  }

  CB_assert(0 && "unreachable");
 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

void cmd_sokol_flags(CB_Arena *arena, CB_Command *cmd)
{
  cb_cmd_append_lits(arena, cmd, "-I./vendor/sokol/", "-Lbuild/", "-lsokol");
  cb_cmd_append_lits(arena, cmd, "-DSOKOL_GLCORE33");
  cb_cmd_append_lits(arena, cmd, "-pthread");
  cb_cmd_append_lits(arena, cmd, "-lGL");
  cb_cmd_append_lits(arena, cmd, "-lX11", "-lXi", "-lXcursor");
}

CB_b32 shdc_compile_shader(CB_Str shader, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  CB_Write_Buffer *b = cb_mem_buffer(scratch.arena, 1024);
  CB_Str_Mark mark = cb_write_buffer_mark(b);
  cb_append_strs(b, shader, S(".h"));
  CB_Str shader_h = cb_str_from_mark(&mark);

  int status = cb_needs_rebuild(shader_h, &shader, 1, stderr);
  if (status <  0) { cb_return_defer(0); }
  if (status == 0) { cb_return_defer(1); }
  if (status >  0) {
    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    cb_cmd_append_lits(scratch.arena, &cmd, "./vendor/sokol-tools-bin/bin/linux/sokol-shdc");
    cb_cmd_append_lits(scratch.arena, &cmd, "-l", "glsl330");
    cb_cmd_append_strs(scratch.arena, &cmd, S("-i"), shader);
    cb_cmd_append_strs(scratch.arena, &cmd, S("-o"), shader_h);
    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
    cb_return_defer(1);
  }

  CB_assert(0 && "unreachable");
 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

CB_b32 build_sokol_example(CB_Str program, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  CB_Write_Buffer *b = cb_mem_buffer(scratch.arena, 1024);

  CB_Str_Mark mark = cb_write_buffer_mark(b);
  cb_append_strs(b, S("./src/sokol-examples/"), program, S(".c"));
  CB_Str source = cb_str_from_mark(&mark);

  cb_append_strs(b, S("./src/sokol-examples/"), program, S(".glsl"));
  CB_Str shader = cb_str_from_mark(&mark);

  cb_append_strs(b, S("build/"), program);
  CB_Str exe = cb_str_from_mark(&mark);

  if (!shdc_compile_shader(shader, stderr)) { cb_return_defer(0); }

  CB_Str sapp_sources[] = { source, shader };
  int status = cb_needs_rebuild(exe, sapp_sources, CB_countof(sapp_sources), stderr);
  if (status <  0) { cb_return_defer(0); }
  if (status == 0) { cb_return_defer(1); }
  if (status >  0) {
    cb_log_begin(stderr, CB_LOG_INFO, S("Building Sokol example: "));
    cb_log_end(stderr, program);

    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    cb_cmd_append_lits(scratch.arena, &cmd, "cc");
    cb_cmd_append_strs(scratch.arena, &cmd, S("-o"), exe);
    cb_cmd_append_strs(scratch.arena, &cmd, source);
    cb_cmd_append_lits(scratch.arena, &cmd, "-g");
    cmd_sokol_flags(scratch.arena, &cmd);
    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
    cb_return_defer(1);
  }

  CB_assert(0 && "unreachable");
 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

CB_b32 build_editor(CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;
  CB_Write_Buffer *b = cb_mem_buffer(scratch.arena, 1024);

  CB_Str program_name = S("editor");
  CB_Str source = S("./src/editor.c");
  CB_Str shader = S("./src/editor.glsl");

  CB_Str_Mark mark = cb_write_buffer_mark(b);
  cb_append_strs(b, S("./build/"), program_name);
  CB_Str exe = cb_str_from_mark(&mark);

  if (!shdc_compile_shader(shader, stderr)) { cb_return_defer(0); }

  CB_Str sapp_sources[] = { source, shader };
  int status = cb_needs_rebuild(exe, sapp_sources, CB_countof(sapp_sources), stderr);
  if (status <  0) { cb_return_defer(0); }
  if (status == 0) { cb_return_defer(1); }
  if (status >  0) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Building Editor.c: "));

    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    cb_cmd_append_lits(scratch.arena, &cmd, "cc");
    cb_cmd_append_strs(scratch.arena, &cmd, S("-o"), exe, source);
    cb_cmd_append_lits(scratch.arena, &cmd, "-lm");
    cb_cmd_append_lits(scratch.arena, &cmd, "-fsanitize=undefined,address");
    cb_cmd_append_lits(scratch.arena, &cmd, "-Wall", "-Wextra");
    cb_cmd_append_lits(scratch.arena, &cmd, "-g");
    /* cb_cmd_append_lits(scratch.arena, &cmd, "-O2", "-march=native"); */
    cmd_sokol_flags(scratch.arena, &cmd);
    cmd_freetype_flags(scratch.arena, &cmd);

    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
    cb_return_defer(1);
  }

  CB_assert(0 && "unreachable");
 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

int main(int argc, char **argv)
{
  CB_Arena *perm = cb_alloc_arena(8 * 1024 * 1024);
  CB_Write_Buffer *stderr = cb_fd_buffer(2, perm, 4 * 1024);
  CB_Str_List cbuild_sources = cb_str_dup_list(perm, "cbuild.c", "cbuild.h");

  // REVIEW: Abort if cbuild is not run from project root.

  // Configure program i.e. write default build/config.h if it does not exist.
  CB_b32 user_requested_to_reconfigure = (argc > 1);
  CB_b32 config_h_exists = cb_file_exists(S("build/config.h"), stderr);
  if (config_h_exists < 0) { cb_exit(1); }
  if (config_h_exists == 0 || user_requested_to_reconfigure) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Reconfiguring cbuild ..."));
    if (!cb_mkdir_if_not_exists(S("build"), stderr)) cb_exit(1);

    CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
    CB_Write_Buffer *conf = cb_mem_buffer(scratch.arena, 8 * 1024);

    cb_append_lit(conf, "// One of [ TARGET_WINDOWS, TARGET_LINUX ].\n");
#ifdef _WIN32
    cb_append_lit(conf, "#define TARGET_WINDOWS\n");
#else
    cb_append_lit(conf, "#define TARGET_LINUX\n");
#endif

#if 0
    CB_Command git = cb_da_init(scratch.arena, CB_Command, 32);
    cb_cmd_append_lits(scratch.arena, &git, "git", "rev-parse", "--short", "HEAD");
    cb_cmd_run_sync(git, stderr);
#endif
    CB_Str git_commit = S("ebc58eb");

    cb_append_strs(conf,
                   S("#define GIT_COMMIT \""),
                   git_commit,
                   S("\"\n"));

    cb_append_lit(conf, "// Build sokol sapp-triangle example.\n");
    cb_append_lit(conf, "// #define BUILD_SOKOL_EXAMPLE\n");
    cb_append_lit(conf, "// Enable to build sokol with debug information and disable optimizations.\n");
    cb_append_lit(conf, "// #define SOKOL_DEBUG\n");

    CB_Str content = (CB_Str){.buf = conf->buf, .len = conf->len, };
    if (!cb_write_entire_file(S("build/config.h"), content, stderr)) cb_exit(1);
    cb_log_emit(stderr, CB_LOG_INFO, S("Wrote build/config.h"));
    cb_arena_pop_mark(scratch);

    cb_rebuild_yourself(argc, argv, cbuild_sources, 1, stderr);
  }

  CB_Str_List cbuild_configured_sources = cb_str_dup_list(perm, "cbuild.c", "cbuild.h", "build/config.h");
  cb_rebuild_yourself(argc, argv, cbuild_configured_sources, 0, stderr);

#ifdef CBUILD_CONFIGURED
  { // TODO: Filter comments in config
    cb_log_emit(stderr, CB_LOG_INFO, S("Config:"));
    CB_Read_Result conf = cb_read_entire_file(perm, S("build/config.h"), stderr);
    if (!conf.status) { cb_exit(1); }
    cb_append_str(stderr, conf.file_contents);
  }
  {// Builder program
    cb_log_emit(stderr, CB_LOG_INFO, S("Starting Build ..."));

    if (!build_freetype_library(stderr)) cb_exit(1);
    if (!build_sokol_library(stderr)) cb_exit(1);
#ifdef BUILD_SOKOL_EXAMPLE
    if (!build_sokol_example(S("triangle-sapp"), stderr)) cb_exit(1);
#endif
    if (!build_editor(stderr)) { cb_exit(1); }

    cb_log_emit(stderr, CB_LOG_INFO, S("Done."));
  }
#endif

  cb_flush(stderr);
  cb_free_arena(perm);
  cb_free_scratch_pool();
  return 0;
}
