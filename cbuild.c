// This is free and unencumbered software released into the public domain.

#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv)
{
  CB_Arena *perm = cb_alloc_arena(8 * 1024 * 1024); // permanent arena
  // REVIEW: Move this to global cbuild context.
  CB_Write_Buffer *stderr = cb_fd_buffer(2, perm, 4 * 1024);

  // TODO: If we are not in the directory where cbuild is, abort, or move working directory there

  { // Rebuild Yourself
    if (!cb_mkdir_if_not_exists(S("build"), stderr)) cb_exit(1);

    CB_Str cbuild_sources[2] = { S("cbuild.c"), S("cbuild.h"), };
    int status = cb_needs_rebuild(S("cbuild"), cbuild_sources, CB_countof(cbuild_sources), stderr);
    if (status < 0) { cb_exit(1); }
    else if (status > 0) {
      cb_log_emit(stderr, CB_LOG_INFO, S("Rebuilding cbuild ..."));

      CB_Command cmd = cb_da_init(perm, CB_Command, 128);
      cb_cmd_append_lits(perm, &cmd, "cc", "-o", "build/cbuild.new", "cbuild.c");
#if 1 // debug flags
      cb_cmd_append_lits(perm, &cmd, "-g");
      cb_cmd_append_lits(perm, &cmd, "-Wall", "-Wextra", "-Wshadow", "-Wconversion");
      cb_cmd_append_lits(perm, &cmd, "-fsanitize=undefined");
      cb_cmd_append_lits(perm, &cmd, "-fsanitize=address");
#endif

      if (!cb_cmd_run_sync(cmd, stderr)) { cb_exit(1); }

      // Swap new and old
      if (!cb_rename(S("cbuild"), S("build/cbuild.old"), stderr)) { cb_exit(1); }
      if (!cb_rename(S("build/cbuild.new"), S("cbuild"), stderr)) { cb_exit(1); }

      // Re-run yourself
      // TODO: use platform agnostic call.
      execv(argv[0], argv);
      CB_assert(0 && "unreachable");
    }
  }

  CB_b32 user_requested_to_reconfigure = (argc > 1);
  if (!cb_file_exists(S("build/config.h"), stderr) || user_requested_to_reconfigure) {
    cb_log_emit(stderr, CB_LOG_INFO, S("Reconfiguring cbuild ..."));

    // Configure program i.e. write default build/config.h for current platform.
    // if file build/config.h does not exist -> construct it and recompile cbuild.

    CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);

    CB_i32 conf_fd = cb_open(S("build/config.h"), stderr);
    if (!conf_fd) { cb_exit(1); }

    CB_Write_Buffer *conf = cb_fd_buffer(conf_fd, scratch.arena, 8 * 1024);
    cb_append_lit(conf, "#define TARGET_LINUX\n");
    cb_append_lit(conf, "#define GIT_COMMIT \"arstenenxzcd\"\n");
    cb_append_lit(conf, "#define CBUILD_VERSION 123\n");

    cb_flush(conf);
    cb_log_emit(stderr, CB_LOG_INFO, S("Wrote build/config.h"));
    cb_close(conf_fd, stderr);

    cb_arena_pop_mark(scratch);
  }

  { // Build program
    cb_append_lit(stderr, "TODO: Implement the calls to build an actual project here! For example sokol-examples.\n");
  }

  cb_flush(stderr);
  cb_free_arena(perm);
  cb_free_scratch_pool();
  return 0;
}
