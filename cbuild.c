#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv)
{
  (void)argc;
  size   heap_capacity = 1 * 1024 * 1024;
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
      if (!os_rename("cbuild", "build/cbuild.old", stderr)) { os_exit(1); };

      Command cmd = da_init(heap, Command, 128);
      *(da_push(heap, &cmd)) = S("cc");
      *(da_push(heap, &cmd)) = S("-o");
      *(da_push(heap, &cmd)) = S("cbuild");
      *(da_push(heap, &cmd)) = S("cbuild.c");

      if (!os_run_cmd_sync(cmd, stderr)) { return 1; }

      // Re-run build program
      execv(argv[0], argv);
      assert(0 && "unreachable");
    }
  }

  Command cmd = da_init(heap, Command, 128);
  *(da_push(heap, &cmd)) = S("cc");
  *(da_push(heap, &cmd)) = S("-o");
  *(da_push(heap, &cmd)) = S("cbuild");
  *(da_push(heap, &cmd)) = S("cbuild.c");

  cmd_render(cmd, stderr);
  append_byte(stderr, '\n');

  { // Build program

  }

  flush(stdout);
  flush(stderr);
  free(heap->backing);
  return 0;
}
