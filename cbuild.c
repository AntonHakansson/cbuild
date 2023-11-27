#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv)
{
  (void)argc;
  size   heap_capacity = 8 * 1024 * 1024;
  size stdout_capacity = 8 * 1024;

  Arena heap[1] = { arena_init((u8*)malloc((usize)heap_capacity), heap_capacity) };
  Write_Buffer stdout[1] = { fd_buffer(1, new(heap, u8, stdout_capacity), stdout_capacity) };
  Write_Buffer stderr[1] = { fd_buffer(2, new(heap, u8, stdout_capacity), stdout_capacity) };

  if (!os_mkdir_if_not_exists("build", stderr)) return 1;

  { // Configure program i.e. write default ./build/config.h for current platform.
    // if file ./build/config.h does not exist -> construct it and recompile build program as configured
  }

  { // Rebuild Yourself
    const char *cbuild_source = "cbuild.c";
    int status = os_needs_rebuild("cbuild", &cbuild_source, 1, stderr);
    if (status < 0) { exit(1); }
    else if (status > 0) {

      Command cmd = da_init(heap, Command, 128);
      *(da_push(heap, &cmd)) = S("cc");
      *(da_push(heap, &cmd)) = S("-o");
      *(da_push(heap, &cmd)) = S("./build/cbuild.new");
      *(da_push(heap, &cmd)) = S("cbuild.c");
      *(da_push(heap, &cmd)) = S("-g");
      *(da_push(heap, &cmd)) = S("-Wall");
      *(da_push(heap, &cmd)) = S("-Wextra");
      *(da_push(heap, &cmd)) = S("-Wshadow");
      *(da_push(heap, &cmd)) = S("-fsanitize=address,undefined");

      if (!os_run_cmd_sync(cmd, stderr)) { return 1; }

      // Swap new and old
      if (!os_rename("cbuild", "build/cbuild.old", stderr)) { os_exit(1); };
      if (!os_rename("build/cbuild.new", "cbuild", stderr)) { os_exit(1); };

      // Re-run yourself
      execv(argv[0], argv);
      assert(0 && "unreachable");
    }
  }

  { // Build program
    append_lit(stdout, "TODO: Implement the calls to build an actual project here!\n");
  }

  flush(stdout);
  flush(stderr);
  free(heap->backing);
  return 0;
}
