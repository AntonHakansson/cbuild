#pragma once

////////////////////////////////////////////////////////////////////////////////
//- Types and Utils

typedef __UINT8_TYPE__   u8;
typedef __UINT32_TYPE__  u32;
typedef __INT32_TYPE__   i32;
typedef __INT32_TYPE__   b32;
typedef __INT64_TYPE__   i64;
typedef __UINT64_TYPE__  u64;
typedef __UINTPTR_TYPE__ uptr;
typedef __PTRDIFF_TYPE__ size;
typedef __SIZE_TYPE__    usize;

#define sizeof(s)  (size)sizeof(s)
#define alignof(s) (size)_Alignof(s)
#define countof(s) (sizeof(s) / sizeof(*(s)))
#define assert(c)  while((!(c))) __builtin_unreachable()

////////////////////////////////////////////////////////////////////////////////
//- String slice

#define S(s)        (Str){ .buf = (u8 *)(s), .len = countof((s)) - 1, }
#define S_FMT       "%.*s"
#define S_ARG(s)    (i32)(s).len, (s).buf

typedef struct {
  u8 *buf;
  size len;
} Str;

b32 str_equals(Str a, Str b);

////////////////////////////////////////////////////////////////////////////////
//- Arena Allocator

#define new(a, t, n) (t *) arena_alloc(a, sizeof(t), alignof(t), (n))

typedef struct {
  u8 *backing;
  u8 *at;
  size capacity;
} Arena;

Arena arena_init(u8 *backing, size capacity);
__attribute__((malloc, alloc_size(2,4), alloc_align(3)))
u8 *arena_alloc(Arena *a, size objsize, size align, size count);

////////////////////////////////////////////////////////////////////////////////
//- Buffered IO

typedef struct  {
  u8 *buf;
  i32 capacity;
  i32 len;
  i32 fd;
  _Bool error;
} Write_Buffer;

Write_Buffer write_buffer(u8 *buf, i32 capacity);
Write_Buffer fd_buffer(i32 fd, u8 *buf, i32 capacity);
void flush(Write_Buffer *b);
void    append(Write_Buffer *b, unsigned char *src, i32 len);
#define append_lit(b, s) append(b, (unsigned char*)s, sizeof(s) - 1)
void    append_str(Write_Buffer *b, Str s);
void    append_byte(Write_Buffer *b, unsigned char c);
void    append_long(Write_Buffer *b, long x);

////////////////////////////////////////////////////////////////////////////////
//- Dynamic Array w/ Arena Allocator

#define da_init(a, t, cap) ({                                           \
      t s = {0};                                                        \
      s.capacity = cap;                                                 \
      s.items = (typeof(s.items))                                       \
        arena_alloc((a), sizeof(s.items), _Alignof(s.items), cap);      \
      s;                                                                \
  })

// Allocates on demand
#define da_push(a, s) ({                                                \
      typeof(s) _s = s;                                                 \
      if (_s->len >= _s->capacity) {                                    \
        da_grow((a), (void **)&_s->items, &_s->capacity, &_s->len,      \
                sizeof(*_s->items), _Alignof(*_s->items));              \
      }                                                                 \
      _s->items + _s->len++;                                            \
    })

// Assumes new item fits in capacity
#define da_push_unsafe(s) ({                    \
      assert((s)->len < (s)->capacity);         \
      (s)->items + (s)->len++;                  \
    })

void da_grow(Arena *arena, void **__restrict items, size *__restrict capacity, size *__restrict len, size item_size, size align);


////////////////////////////////////////////////////////////////////////////////
//- Command

typedef struct Command {
  Str *items;
  size capacity;
  size len;
} Command;


////////////////////////////////////////////////////////////////////////////////
//- Platform

void os_exit (i32 status);
b32  os_write(i32 fd, u8 *buf, size len);
b32  os_mkdir_if_not_exists(char *directory);
b32  os_rename(const char *old_path, const char *new_path);
b32  os_needs_rebuild(const char *output_path, const char **input_paths, int input_paths_count);

#define OS_INVALID_PROC (-1)
typedef int OS_Proc;
int os_run_cmd_async(Command command);
b32 os_run_cmd_sync(Command command);
b32 os_proc_wait(OS_Proc proc);




#ifdef CBUILD_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////
//- String slice

b32 str_equals(Str a, Str b)
{
  if (a.len != b.len) { return 0; }
  for (size i = 0; i < a.len; i++) {
    if (a.buf[i] != b.buf[i]) { return 0; }
  }
  return 1;
}


////////////////////////////////////////////////////////////////////////////////
//- Arena Allocator

Arena arena_init(u8 *backing, size capacity)
{
  Arena result = {};
  result.at = result.backing = backing;
  result.capacity = capacity;
  return result;
}

__attribute__((malloc, alloc_size(2,4), alloc_align(3)))
u8 *arena_alloc(Arena *a, size objsize, size align, size count)
{
  size avail = (a->backing + a->capacity) - a->at;
  size padding = -(uptr)a->at & (align - 1);
  size total   = padding + objsize * count;
  if (avail < total) {
    os_write(2, (u8 *)"Out of Memory", 13);
    os_exit(1);
  }

  u8 *p = a->at + padding;
  a->at += total;

  for (size i = 0; i < objsize * count; i++) {
    p[i] = 0;
  }

  return p;
}


////////////////////////////////////////////////////////////////////////////////
//- Buffered IO

Write_Buffer write_buffer(u8 *buf, i32 capacity)
{
  Write_Buffer result = {0};
  result.buf = buf;
  result.capacity = capacity;
  result.fd = -1;
  return result;
}

Write_Buffer fd_buffer(i32 fd, u8 *buf, i32 capacity)
{
  Write_Buffer result = {0};
  result.buf = buf;
  result.capacity = capacity;
  result.fd = fd;
  return result;
}

void append(Write_Buffer *b, unsigned char *src, i32 len)
{
  unsigned char *end = src + len;
  while (!b->error && src<end) {
    i32 left = end - src;
    i32 avail = b->capacity - b->len;
    i32 amount = avail<left ? avail : left;

    for (i32 i = 0; i < amount; i++) {
      b->buf[b->len+i] = src[i];
    }
    b->len += amount;
    src += amount;

    if (amount < left) {
      flush(b);
    }
  }
}

#define append_lit(b, s) append(b, (unsigned char*)s, sizeof(s) - 1)

void append_str(Write_Buffer *b, Str s)
{
  append(b, s.buf, s.len);
}

void append_byte(Write_Buffer *b, unsigned char c)
{
  append(b, &c, 1);
}

void append_long(Write_Buffer *b, long x)
{
  unsigned char tmp[64];
  unsigned char *end = tmp + sizeof(tmp);
  unsigned char *beg = end;
  long t = x>0 ? -x : x;
  do {
    *--beg = '0' - t%10;
  } while (t /= 10);
  if (x < 0) {
    *--beg = '-';
  }
  append(b, beg, end-beg);
}

void flush(Write_Buffer *b)
{
  b->error |= b->fd < 0;
  if (!b->error && b->len) {
    b->error |= !os_write(b->fd, b->buf, b->len);
    b->len = 0;
  }
}

////////////////////////////////////////////////////////////////////////////////
//- Dynamic Array w/ Arena Allocator

void da_grow(Arena *arena, void **__restrict items, size *__restrict capacity, size *__restrict len, size item_size, size align)
{
  assert(*items != 0 && *capacity > 0);
  u8 *items_end = (((u8*)(*items)) + (item_size * (*len)));
  if (arena->at == items_end) {
    // Extend in place, no allocation occured between da_grow calls
    arena_alloc(arena, item_size, align, (*capacity));
    *capacity *= 2;
  }
  else {
    // Relocate array
    u8 *p = arena_alloc(arena, item_size, align, (*capacity) * 2);
#define DA_MEMORY_COPY(dst, src, bytes) for (int i = 0; i < bytes; i++) { ((char *)dst)[i] = ((char *)src)[i]; }
    DA_MEMORY_COPY(p, *items, (*len) * item_size);
#undef DA_MEMORY_COPY
    *items = (void *)p;
    *capacity *= 2;
  }
}

////////////////////////////////////////////////////////////////////////////////
//- Platform Agnostic Layer

#ifdef _WIN32
#error "windows api not implemented."
#else

#include <stdlib.h> // malloc
#include <stdio.h> // rename
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

b32 os_write(i32 fd, u8 *buf, size len)
{
  for (size off = 0; off < len;) {
    size written = write(fd, buf + off, len - off);
    if (written < 1) { return 0; }
    off += written;
  }
  return 1;
}

void os_exit (int status)
{
  exit(status);
}

b32 os_mkdir_if_not_exists(char *directory)
{
  int result = mkdir(directory, 0755);
  if (result < 0) {
    if (errno == EEXIST) {
      return 1;
    }

    // TODO: log error here
    return 0;
  }

  // TODO: Log what we did.
  return 1;
}

b32 os_rename(const char *old_path, const char *new_path)
{
  if (rename(old_path, new_path) < 0) {
    /* nob_log(NOB_ERROR, "could not rename %s to %s: %s", old_path, new_path, strerror(errno)); */
    return 0;
  }
  return 1;
}

b32 os_needs_rebuild(const char *output_path, const char **input_paths, int input_paths_count)
{
  struct stat statbuf = {0};

  if (stat(output_path, &statbuf) < 0) {
    // NOTE: if output does not exist it 100% must be rebuilt
    if (errno == ENOENT) return 1;
    /* nob_log(NOB_ERROR, "could not stat %s: %s", output_path, strerror(errno)); */
    return -1;
  }
  int output_path_time = statbuf.st_mtime;

  for (size_t i = 0; i < input_paths_count; ++i) {
    const char *input_path = input_paths[i];
    if (stat(input_path, &statbuf) < 0) {
      // NOTE: non-existing input is an error cause it is needed for building in the first place
      /* nob_log(NOB_ERROR, "could not stat %s: %s", input_path, strerror(errno)); */
      return -1;
    }
    int input_path_time = statbuf.st_mtime;
    // NOTE: if even a single input_path is fresher than output_path that's 100% rebuild
    if (input_path_time > output_path_time) return 1;
  }

  return 0;
}

int os_run_cmd_async(Command command)
{
  if (command.len < 1) {
    // TODO: log error
    return OS_INVALID_PROC;
  }

  pid_t cpid = fork();
  if (cpid < 0) {
    // TODO: log "Could not fork child process: %s", strerror(errno));
    return OS_INVALID_PROC;
  }

  if (cpid == 0) {
    u8 cmd_mem[1 * 1024 * 1024];
    char *cmd_null[512];
    {
      Write_Buffer b[1] = { write_buffer(cmd_mem, 1 * 1024 * 1024) };
      size i = 0;
      for (i = 0; i < command.len; i++) {
        u8 *cmd_cstr = b->buf + b->len;
        cmd_null[i] = cmd_cstr;
        append_str(b, command.items[i]);
        append_byte(b, 0);
      }
      assert(i < 512);
      assert(!b->error);
      cmd_null[i] = 0;
    }

    if (execvp(cmd_null[0], cmd_null) < 0) {
      // TODO: log error
    }
    assert(0 && "unreachable");
  }

  return cpid;
}

b32 os_run_cmd_sync(Command command)
{
  OS_Proc proc = os_run_cmd_async(command);
  if (!os_proc_wait(proc)) {
    /* append_str(stdout, S("[ERROR]: Failed to wait on proc.\n")); */
    return 0;
  }
  return 1;
}

b32 os_proc_wait(OS_Proc proc)
{
  if (proc == OS_INVALID_PROC) return 0;

  for (;;) {
    int wstatus = 0;
    if (waitpid(proc, &wstatus, 0) < 0) {
      /* nob_log(NOB_ERROR, "could not wait on command (pid %d): %s", proc, strerror(errno)); */
      return 0;
    }

    if (WIFEXITED(wstatus)) {
      int exit_status = WEXITSTATUS(wstatus);
      if (exit_status != 0) {
        /* nob_log(NOB_ERROR, "command exited with exit code %d", exit_status); */
        return 0;
      }

      break;
    }

    if (WIFSIGNALED(wstatus)) {
      /* nob_log(NOB_ERROR, "command process was terminated by %s", strsignal(WTERMSIG(wstatus))); */
      return 0;
    }
  }

  return 1;
}


#endif // __LINUX__

#endif // CBUILD_IMPLEMENTATION
