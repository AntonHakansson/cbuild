// TODO: Credit tsoding properly

#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv)
{
  Arena *heap = alloc_arena(8 * 1024 * 1024);
  Write_Buffer *stderr = fd_buffer(2, heap, 4 * 1024);

  // TODO: If we are not in the directory where cbuild is, abort, or move working directory there

  { // Rebuild Yourself
    if (!os_mkdir_if_not_exists("build", stderr)) return 1;

    const char *cbuild_sources[2] = { "cbuild.c", "cbuild.h", };
    int status = os_needs_rebuild("cbuild", cbuild_sources, countof(cbuild_sources), stderr);
    if (status < 0) { os_exit(1); }
    else if (status > 0) {
      log_emit(stderr, LOG_INFO, S("Rebuilding cbuild ..."));

      Command cmd = da_init(heap, Command, 128);
      // TODO: clean up with nicer interface, ex: VA_ARGS
      *(da_push(heap, &cmd)) = S("cc");
      *(da_push(heap, &cmd)) = S("-o");
      *(da_push(heap, &cmd)) = S("build/cbuild.new");
      *(da_push(heap, &cmd)) = S("cbuild.c");
#if 1 // debug flags
      *(da_push(heap, &cmd)) = S("-g");
      *(da_push(heap, &cmd)) = S("-Wall");
      *(da_push(heap, &cmd)) = S("-Wextra");
      *(da_push(heap, &cmd)) = S("-Wshadow");
      *(da_push(heap, &cmd)) = S("-fsanitize=address,undefined");
#endif

      if (!os_run_cmd_sync(cmd, stderr)) { os_exit(1); }

      // Swap new and old
      if (!os_rename("cbuild", "build/cbuild.old", stderr)) { os_exit(1); };
      if (!os_rename("build/cbuild.new", "cbuild", stderr)) { os_exit(1); };

      // Re-run yourself
      // TODO: use platform agnostic call.
      execv(argv[0], argv);
      assert(0 && "unreachable");
    }
  }

  b32 user_requested_to_reconfigure = (argc > 1);
  if (!os_file_exists("build/config.h", stderr) || user_requested_to_reconfigure) {
    log_emit(stderr, LOG_INFO, S("Reconfiguring cbuild ..."));

    // Configure program i.e. write default build/config.h for current platform.
    // if file build/config.h does not exist -> construct it and recompile cbuild.

    Arena_Mark scratch = arena_get_scratch(&heap, 1);

    i32 conf_fd = os_open("build/config.h", stderr);
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
