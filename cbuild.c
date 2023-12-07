// This is free and unencumbered software released into the public domain.

#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

#ifdef CBUILD_CONFIGURED
#include "build/config.h"
#endif

CB_b32 build_sokol_library(CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  CB_Str sokol_out = S("build/libsokol.a");
  CB_Str sokol_sources[] = {
    S("src/sokol.c"),
    S("vendor/sokol/sokol_app.h"),
    S("vendor/sokol/sokol_gfx.h"),
    S("vendor/sokol/sokol_time.h"),
    S("vendor/sokol/sokol_fetch.h"),
    S("vendor/sokol/sokol_log.h"),
    S("vendor/sokol/sokol_glue.h"),
  };
  int status = cb_needs_rebuild(sokol_out, sokol_sources, CB_countof(sokol_sources), stderr);
  if (status < 0) { cb_return_defer(0); }
  else if (status > 0) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Building Sokol Library ..."));
    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    char *sokol_out_cstr = cb_str_to_cstr(scratch.arena, sokol_out);
    cb_cmd_append_lits(scratch.arena, &cmd, "cc", "-o", sokol_out_cstr, "-c", "src/sokol.c");
    cb_cmd_append_lits(scratch.arena, &cmd, "-I./vendor/sokol/");
    cb_cmd_append_lits(scratch.arena, &cmd, "-DSOKOL_GLCORE33");
#if SOKOL_DEBUG
    cb_cmd_append_lits(scratch.arena, &cmd, "-g");
#else
     cb_cmd_append_lits(scratch.arena, &cmd, "-O2");
#endif

    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
  }

  cb_return_defer(1);

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
  cb_append_lit(b, "./src/");
  cb_append_str(b, program);
  cb_append_lit(b, ".c");
  CB_Str source = cb_str_from_mark(&mark);

  cb_append_lit(b, "./src/");
  cb_append_str(b, program);
  cb_append_lit(b, ".glsl");
  CB_Str shader = cb_str_from_mark(&mark);

  cb_append_lit(b, "./src/");
  cb_append_str(b, program);
  cb_append_lit(b, ".glsl");
  cb_append_lit(b, ".h");
  CB_Str shader_h =  cb_str_from_mark(&mark);

  int status_shader = cb_needs_rebuild(shader_h, &shader, 1, stderr);
  if (status_shader < 0) { cb_return_defer(0); }
  else if (status_shader > 0) {
    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    char *shader_cstr = cb_str_to_cstr(scratch.arena, shader);
    char *shader_h_cstr = cb_str_to_cstr(scratch.arena, shader_h);
    cb_cmd_append_lits(scratch.arena, &cmd, "./vendor/sokol-tools-bin/bin/linux/sokol-shdc");
    cb_cmd_append_lits(scratch.arena, &cmd, "-l", "glsl330");
    cb_cmd_append_lits(scratch.arena, &cmd, "-i", shader_cstr);
    cb_cmd_append_lits(scratch.arena, &cmd, "-o", shader_h_cstr);

    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
  }

  mark = cb_write_buffer_mark(b);
  cb_append_lit(b, "build/");
  cb_append_str(b, program);
  CB_Str exe = cb_str_from_mark(&mark);

  CB_Str sapp_sources[] = { source, shader };
  int status = cb_needs_rebuild(exe, sapp_sources, CB_countof(sapp_sources), stderr);
  if (status < 0) { cb_return_defer(0); }
  else if (status > 0) {
    cb_log_begin(stderr, CB_LOG_INFO, S("Building Sokol example: "));
    cb_log_end(stderr, program);

    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    char *exe_cstr = cb_str_to_cstr(scratch.arena, exe);
    char *program_cstr = cb_str_to_cstr(scratch.arena, source);
    cb_cmd_append_lits(scratch.arena, &cmd, "cc", "-o", exe_cstr, program_cstr);
    cb_cmd_append_lits(scratch.arena, &cmd, "-I./vendor/sokol/", "-Lbuild/", "-lsokol");
    cb_cmd_append_lits(scratch.arena, &cmd, "-DSOKOL_GLCORE33");
    cb_cmd_append_lits(scratch.arena, &cmd, "-pthread");
    cb_cmd_append_lits(scratch.arena, &cmd, "-lGL");
    cb_cmd_append_lits(scratch.arena, &cmd, "-lX11", "-lXi", "-lXcursor");
    cb_cmd_append_lits(scratch.arena, &cmd, "-g");

    if (!cb_cmd_run_sync(cmd, stderr)) { cb_return_defer(0); }
  }
  cb_return_defer(1);

 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

int main(int argc, char **argv)
{
  CB_Arena *perm = cb_alloc_arena(8 * 1024 * 1024);
  CB_Write_Buffer *stderr = cb_fd_buffer(2, perm, 4 * 1024);
  CB_Str_List cbuild_sources = cb_str_dup_list(perm, "cbuild.c", "cbuild.h");

  // Configure program i.e. write default build/config.h if it does not exist.
  CB_b32 user_requested_to_reconfigure = (argc > 1);
  if (!cb_file_exists(S("build/config.h"), stderr) || user_requested_to_reconfigure) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Reconfiguring cbuild ..."));
    if (!cb_mkdir_if_not_exists(S("build"), stderr)) cb_exit(1);

    CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
    CB_Write_Buffer *conf = cb_mem_buffer(scratch.arena, 8 * 1024);

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

    cb_append_lit(conf, "#define GIT_COMMIT \"");
      cb_append_str(conf, git_commit);
    cb_append_lit(conf, "\"\n");

    cb_append_lit(conf, "#define BUILD_SOKOL_EXAMPLE 1\n");
    cb_append_lit(conf, "// #define SOKOL_DEBUG 1\n");

    CB_Str content = (CB_Str){.buf = conf->buf, .len = conf->len, };
    if (!cb_write_entire_file(S("build/config.h"), content, stderr)) cb_exit(1);
    cb_log_emit(stderr, CB_LOG_INFO, S("Wrote build/config.h"));
    cb_arena_pop_mark(scratch);

    cb_rebuild_yourself(argc, argv, cbuild_sources, 1, stderr);
  }

  CB_Str_List cbuild_configured_sources = cb_str_dup_list(perm, "cbuild.c", "cbuild.h", "build/config.h");
  cb_rebuild_yourself(argc, argv, cbuild_configured_sources, 0, stderr);

#ifdef CBUILD_CONFIGURED

  cb_log_emit(stderr, CB_LOG_INFO, S("Config:"));
  CB_Read_Result conf = cb_read_entire_file(perm, S("build/config.h"), stderr);
  if (!conf.status) { cb_exit(1); }
  cb_append_str(stderr, conf.file_contents);

  { // Builder program
    cb_log_emit(stderr, CB_LOG_INFO, S("Starting Build ..."));
#ifdef BUILD_SOKOL_EXAMPLE
    if (!build_sokol_library(stderr)) cb_exit(1);
    if (!build_sokol_example(S("triangle-sapp"), stderr)) cb_exit(1);
#endif
  }
#endif

  cb_flush(stderr);
  cb_free_arena(perm);
  cb_free_scratch_pool();
  return 0;
}
