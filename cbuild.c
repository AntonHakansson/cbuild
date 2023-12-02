// TODO: Credit tsoding properly

#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv)
{
  Arena *heap = alloc_arena(8 * 1024 * 1024);
  // REVIEW: Move this to global cbuild context.
  Write_Buffer *stderr = fd_buffer(2, heap, 4 * 1024);

  // TODO: If we are not in the directory where cbuild is, abort, or move working directory there

  { // Rebuild Yourself
    if (!os_mkdir_if_not_exists(S("build"), stderr)) os_exit(1);

    Str cbuild_sources[2] = { S("cbuild.c"), S("cbuild.h"), };
    int status = os_needs_rebuild(S("cbuild"), cbuild_sources, countof(cbuild_sources), stderr);
    if (status < 0) { os_exit(1); }
    else if (status > 0) {
      log_emit(stderr, LOG_INFO, S("Rebuilding cbuild ..."));

      Command cmd = da_init(heap, Command, 128);
      cmd_append_lits(heap, &cmd, "cc", "-o", "build/cbuild.new", "cbuild.c");
#if 1 // debug flags
      cmd_append_lits(heap, &cmd, "-g");
      cmd_append_lits(heap, &cmd, "-Wall", "-Wextra", "-Wshadow");
      cmd_append_lits(heap, &cmd, "-fsanitize=undefined");
      cmd_append_lits(heap, &cmd, "-fsanitize=address");
#endif

      if (!os_run_cmd_sync(cmd, stderr)) { os_exit(1); }

      // Swap new and old
      if (!os_rename(S("cbuild"), S("build/cbuild.old"), stderr)) { os_exit(1); };
      if (!os_rename(S("build/cbuild.new"), S("cbuild"), stderr)) { os_exit(1); };

      // Re-run yourself
      // TODO: use platform agnostic call.
      execv(argv[0], argv);
      assert(0 && "unreachable");
    }
  }

  b32 user_requested_to_reconfigure = (argc > 1);
  if (!os_file_exists(S("build/config.h"), stderr) || user_requested_to_reconfigure) {
    log_emit(stderr, LOG_INFO, S("Reconfiguring cbuild ..."));

    // Configure program i.e. write default build/config.h for current platform.
    // if file build/config.h does not exist -> construct it and recompile cbuild.

    Arena_Mark scratch = arena_get_scratch(0, 0);

    i32 conf_fd = os_open(S("build/config.h"), stderr);
    if (!conf_fd) { os_exit(1); }

    Write_Buffer *conf = fd_buffer(conf_fd, scratch.arena, 8 * 1024);
    append_lit(conf, "#define TARGET_LINUX\n");
    append_lit(conf, "#define GIT_COMMIT \"arstenenxzcd\"\n");
    append_lit(conf, "#define CBUILD_VERSION 123\n");

    flush(conf);
    log_emit(stderr, LOG_INFO, S("Wrote build/config.h"));
    os_close(conf_fd, stderr);

    arena_pop_mark(scratch);
  }

  { // Build program
    append_lit(stderr, "TODO: Implement the calls to build an actual project here! For example sokol-examples.\n");
  }

  flush(stderr);
  free_arena(heap);
  free_scratch_pool();
  return 0;
}
