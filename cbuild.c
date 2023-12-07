// This is free and unencumbered software released into the public domain.

#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

CB_b32 build_sokol_library(CB_b32 debug, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  CB_Str sokol_sources[] = {
    S("src/sokol.c"),
    S("vendor/sokol/sokol_app.h"),
    S("vendor/sokol/sokol_gfx.h"),
    S("vendor/sokol/sokol_time.h"),
    S("vendor/sokol/sokol_fetch.h"),
    S("vendor/sokol/sokol_log.h"),
    S("vendor/sokol/sokol_glue.h"),
  };
  int status = cb_needs_rebuild(S("build/libsokol.a"), sokol_sources, CB_countof(sokol_sources), stderr);
  if (status < 0) { cb_return_defer(0); }
  else if (status > 0) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Building Sokol Library"));
    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 64);
    cb_cmd_append_lits(scratch.arena, &cmd, "cc", "-o", "build/libsokol.a", "-c", "src/sokol.c");
    cb_cmd_append_lits(scratch.arena, &cmd, "-I./vendor/sokol/");
    cb_cmd_append_lits(scratch.arena, &cmd, "-DSOKOL_GLCORE33");
    if (debug) cb_cmd_append_lits(scratch.arena, &cmd, "-g");
    else       cb_cmd_append_lits(scratch.arena, &cmd, "-O2");

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
  CB_Str source = {0};
  source.buf = b->buf + b->len;
  cb_append_lit(b, "./src/");
  cb_append_str(b, program);
  cb_append_lit(b, ".c");
  source.len = (b->buf + b->len) - source.buf;

  CB_Str shader = {0};
  shader.buf = b->buf + b->len;
  cb_append_lit(b, "./src/");
  cb_append_str(b, program);
  cb_append_lit(b, ".glsl");
  shader.len = (b->buf + b->len) - shader.buf;
  cb_append_lit(b, ".h");
  CB_Str shader_h = (CB_Str){.buf = shader.buf, .len = shader.len + 2, };

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

  CB_Str exe = {0};
  exe.buf = b->buf + b->len;
  cb_append_lit(b, "build/");
  cb_append_str(b, program);
  exe.len = (b->buf + b->len) - exe.buf;

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

void cb_rebuild_yourself(int argc, char **argv, CB_Str_List sources, CB_Write_Buffer *stderr)
{
  (void)argc;
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  if (!cb_mkdir_if_not_exists(S("build"), stderr)) cb_exit(1);

  int status = cb_needs_rebuild(S("cbuild"), sources.items, sources.len, stderr);
  if (status < 0) { cb_exit(1); }
  else if (status > 0) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Rebuilding cbuild ..."));

    CB_Command cmd = cb_da_init(scratch.arena, CB_Command, 128);
    cb_cmd_append_lits(scratch.arena, &cmd, "cc", "-o", "build/cbuild.new", "cbuild.c");
    cb_cmd_append_lits(scratch.arena, &cmd, "-g3");
    cb_cmd_append_lits(scratch.arena, &cmd, "-Wall", "-Wextra", "-Wshadow", "-Wconversion");
    cb_cmd_append_lits(scratch.arena, &cmd, "-fsanitize=undefined");
    cb_cmd_append_lits(scratch.arena, &cmd, "-fsanitize=address");

    if (!cb_cmd_run_sync(cmd, stderr)) { cb_exit(1); }

    // Swap new and old
    if (!cb_rename(S("cbuild"), S("build/cbuild.old"), stderr)) { cb_exit(1); }
    if (!cb_rename(S("build/cbuild.new"), S("cbuild"), stderr)) { cb_exit(1); }

    // Re-run yourself
    execv(argv[0], argv);
    CB_assert(0 && "unreachable");
  }

  cb_arena_pop_mark(scratch);
}

int main(int argc, char **argv)
{
  // Permanent arena - lifetime of whole program
  CB_Arena *perm = cb_alloc_arena(8 * 1024 * 1024);
  CB_Write_Buffer *stderr = cb_fd_buffer(2, perm, 4 * 1024);
  CB_Str_List cbuild_sources = cb_str_dup_list(perm, "cbuild.c", "cbuild.h");
  cb_rebuild_yourself(argc, argv, cbuild_sources, stderr);

  CB_b32 user_requested_to_reconfigure = (argc > 1);
  if (!cb_file_exists(S("build/config.h"), stderr) || user_requested_to_reconfigure) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Reconfiguring cbuild ..."));

    // Configure program i.e. write default build/config.h for current platform.
    // if file build/config.h does not exist -> construct it and recompile cbuild.

    CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);

    CB_Write_Buffer *conf = cb_mem_buffer(scratch.arena, 8 * 1024);
    cb_append_lit(conf, "#define TARGET_LINUX\n");

#if 0
    CB_Command git = cb_da_init(scratch.arena, CB_Command, 32);
    cb_cmd_append_lits(scratch.arena, &git, "git", "rev-parse", "--short", "HEAD");
    cb_cmd_run_sync(git, stderr);
#endif
    CB_Str git_commit = S("ebc58eb");

    cb_append_lit(conf, "#define GIT_COMMIT \"");
      cb_append_str(conf, git_commit);
    cb_append_lit(conf, "\"\n");

    CB_Str content = (CB_Str){.buf = conf->buf, .len = conf->len, };
    if (!cb_write_entire_file(S("build/config.h"), content, stderr)) cb_exit(1);

    cb_arena_pop_mark(scratch);
  }

  { // Builder program
    CB_b32 debug = 1;

    if (!build_sokol_library(debug, stderr)) cb_exit(1);
    if (!build_sokol_example(S("triangle-sapp"), stderr)) cb_exit(1);
  }

  cb_flush(stderr);
  cb_free_arena(perm);
  cb_free_scratch_pool();
  return 0;
}
