#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

int main(int argc, char **argv)
{
  size   heap_capacity = 1 * 1024 * 1024;
  size stdout_capacity = 8 * 1024;

  Arena heap[1] = { arena_init((u8*)malloc(heap_capacity), heap_capacity) };
  Write_Buffer stdout[1] = { fd_buffer(1, new(heap, u8, stdout_capacity), stdout_capacity) };

  if (!os_mkdir_if_not_exists("build")) return 1;
  append_str(stdout, S("[LOG]: Creating directory ./build\n"));

  const char *cbuild_source = "cbuild.c";
  int status = os_needs_rebuild("cbuild", &cbuild_source, 1);
  if (status < 0) { exit(1); }
  else if (status > 0) {
    if (!os_rename("./cbuild", "./build/cbuild.old")) exit(1);
    append_str(stdout, S("[LOG]: Renaming ./cbuild to ./build/cbuild.old\n"));
    append_str(stdout, S("[LOG]: Compiling cbuild ...\n"));
    flush(stdout);

    Command cmd = da_init(heap, Command, 128);
    *(da_push(heap, &cmd)) = S("cc");
    *(da_push(heap, &cmd)) = S("-o");
    *(da_push(heap, &cmd)) = S("cbuild");
    *(da_push(heap, &cmd)) = S("cbuild.c");

    if (!os_run_cmd_sync(cmd)) {
      append_str(stdout, S("[ERROR]: Failed to wait on proc.\n"));
      return 1;
    }

    cmd.len = 0;
    *(da_push(heap, &cmd)) = S("cbuild");
    execv(argv[0], argv);
    // TODO: exec cbuild.
  }

  flush(stdout);

  return 0;
}
