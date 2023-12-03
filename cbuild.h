// This is free and unencumbered software released into the public domain.
#pragma once

#if !defined(CB_DEFAULT_BASIC_TYPES)
#  define CB_DEFAULT_BASIC_TYPES 1
#endif


////////////////////////////////////////////////////////////////////////////////
//- Types and Utils
//
// REVIEW: Introduce a byte type that aliases i.e. char. Right now I use u8 all over the
// place that AFAIK does not strictly alias other types.
//

#if defined(CB_DEFAULT_BASIC_TYPES)
#include <stdint.h>
typedef uint8_t   CB_u8;
typedef uint32_t  CB_u32;
typedef int32_t   CB_i32;
typedef int64_t   CB_i64;
typedef uint64_t  CB_u64;
typedef uintptr_t CB_uptr;
// REVIEW: @portability
typedef __PTRDIFF_TYPE__ CB_size;
typedef __SIZE_TYPE__    CB_usize;
#endif // CB_DEFAULT_BASIC_TYPES

typedef CB_i32   CB_b32;

// Use signed values everywhere
#define CB_sizeof(s)  (CB_size)sizeof(s)
#define CB_countof(s) (CB_sizeof(s) / CB_sizeof(*(s)))
 // REVIEW: @portability
#define CB_alignof(s) (CB_size)_Alignof(s)
#define CB_assert(c)  while((!(c))) __builtin_unreachable()

#define cb_return_defer(r)  do { result = (r); goto defer; } while(0)

////////////////////////////////////////////////////////////////////////////////
//- Arena Allocator

#define new(a, t, n) (t *) arena_alloc(a, CB_sizeof(t), CB_alignof(t), (n))

typedef struct {
  CB_u8 *backing;
  CB_u8 *at;
  CB_size capacity;
} Arena;

Arena *alloc_arena(CB_size capacity);
void   free_arena(Arena *arena);

Arena arena_init(CB_u8 *backing, CB_size capacity);
__attribute__((malloc, alloc_size(2,4), alloc_align(3)))
CB_u8 *arena_alloc(Arena *a, CB_size objsize, CB_size align, CB_size count);

typedef struct {
  Arena *arena;
  CB_u8 *marker;
} Arena_Mark;

Arena_Mark arena_push_mark(Arena *a);
void arena_pop_mark(Arena_Mark a);

#define SCRATCH_ARENA_COUNT 2
#define SCRATCH_ARENA_CAPACITY (8 * 1024 * 1024)

Arena_Mark arena_scratch(Arena **conflicts, CB_size conflicts_len);
void free_scratch_pool();


////////////////////////////////////////////////////////////////////////////////
//- String slice

#define S(s)        (Str){ .buf = (CB_u8 *)(s), .len = CB_countof((s)) - 1, }
#define S_FMT       "%.*s"
#define S_ARG(s)    (i32)(s).len, (s).buf

typedef struct {
  CB_u8 *buf;
  CB_size len;
} Str;

Str str_from_cstr(char *str);
CB_b32 str_equals(Str a, Str b);
char *str_to_cstr(Arena *a, Str s);


////////////////////////////////////////////////////////////////////////////////
//- Buffered IO

typedef struct  {
  CB_u8 *buf;
  CB_size capacity;
  CB_size len;
  CB_i32 fd;
  CB_b32 error;
} Write_Buffer;

Write_Buffer *mem_buffer(Arena *a, CB_size capacity);
Write_Buffer *fd_buffer(CB_i32 fd, Arena *a, CB_size capacity);
void flush(Write_Buffer *b);

void    append(Write_Buffer *b, unsigned char *src, CB_size len);
#define append_lit(b, s) append(b, (unsigned char*)s, CB_sizeof(s) - 1)
void    append_str(Write_Buffer *b, Str s);
void    append_byte(Write_Buffer *b, unsigned char c);
void    append_long(Write_Buffer *b, long x);


////////////////////////////////////////////////////////////////////////////////
//- Dynamic Array

#define da_init(a, t, cap) ({                                           \
      t s = {0};                                                        \
      s.capacity = cap;                                                 \
      s.items = (typeof(s.items))                                       \
        arena_alloc((a), CB_sizeof(*s.items), CB_alignof(typeof(*s.items)), s.capacity); \
      s;                                                                \
  })

// Allocates on demand
#define da_push(a, s) ({                                                \
      typeof(s) _s = s;                                                 \
      if (_s->len >= _s->capacity) {                                    \
        da_grow((a), (void **)&_s->items, &_s->capacity, &_s->len,      \
                CB_sizeof(*_s->items), CB_alignof(typeof(*_s->items)));       \
      }                                                                 \
      _s->items + _s->len++;                                            \
    })

// Assumes new item fits in capacity
#define da_push_unsafe(s) ({                    \
      CB_assert((s)->len < (s)->capacity);         \
      (s)->items + (s)->len++;                  \
    })

void da_grow(Arena *arena, void **items, CB_size *__restrict capacity, CB_size *__restrict len, CB_size item_size, CB_size align);


////////////////////////////////////////////////////////////////////////////////
//- Log

typedef enum Log_Level {
  LOG_ERROR,
  LOG_WARNING,
  LOG_INFO,

  LOG_COUNT,
} Log_Level;

void log_begin(Write_Buffer *b, Log_Level level, Str prefix);
void log_end(Write_Buffer *b, Str suffix);
void log_emit(Write_Buffer *b, Log_Level level, Str fmt);


////////////////////////////////////////////////////////////////////////////////
//- Command

typedef struct Command {
  Str *items;
  CB_size capacity;
  CB_size len;
} Command;

Str  cmd_render(Command cmd, Write_Buffer *buf);
void cmd_append(Arena *arena, Command *cmd, Str arg);
void cmd_append_lits_(Arena *arena, Command *cmd, const char *lits[],
                      CB_size lits_len);
#define cmd_append_lits(arena, cmd, ...) \
  cmd_append_lits_(arena, cmd, ((const char*[]){__VA_ARGS__}), (CB_sizeof(((const char*[]){__VA_ARGS__}))/(CB_sizeof(const char*))))


////////////////////////////////////////////////////////////////////////////////
//- Platform

CB_u8  *os_malloc(CB_size amount);
void os_mfree(CB_u8 *memory_to_free);
__attribute__((noreturn)) void os_exit (CB_i32 status);
CB_b32  os_write(CB_i32 fd, CB_u8 *buf, CB_size len);
CB_i32  os_open(Str filepath, Write_Buffer *stderr);
CB_b32  os_close(CB_i32 fd, Write_Buffer *stderr);
CB_b32  os_file_exists(Str filepath, Write_Buffer *stderr);
CB_b32  os_mkdir_if_not_exists(Str directory, Write_Buffer *stderr);
CB_b32  os_rename(Str old_path, Str new_path, Write_Buffer *stderr);
CB_b32  os_needs_rebuild(Str output_path, Str *input_paths, int input_paths_len, Write_Buffer *stderr);

#define OS_INVALID_PROC (-1)
typedef int OS_Proc;
int os_run_cmd_async(Command command, Write_Buffer *stderr);
CB_b32 os_run_cmd_sync(Command command, Write_Buffer* stderr);
CB_b32 os_proc_wait(OS_Proc proc, Write_Buffer *stderr);




#ifdef CBUILD_IMPLEMENTATION

///////////////////////////////////////////////////////////////////////////////
//- String slice Implemntation

Str str_from_cstr(char *str)
{
  CB_assert(str);
  Str result = {0};
  result.buf = (CB_u8 *)str;

  while (*str) {
    result.len++;
    str++;
  }

  return result;
}

CB_b32 str_equals(Str a, Str b)
{
  if (a.len != b.len) { return 0; }
  for (CB_size i = 0; i < a.len; i++) {
    if (a.buf[i] != b.buf[i]) { return 0; }
  }
  return 1;
}

char *str_to_cstr(Arena *a, Str s)
{
  Write_Buffer *b = mem_buffer(a, s.len + 1);
  append_str(b, s);
  append_byte(b, '\0');
  return (char *)b->buf;
}


////////////////////////////////////////////////////////////////////////////////
//- Arena Allocator Implementation

#if defined(__SANITIZE_ADDRESS__)
#  include <sanitizer/asan_interface.h>
#  define ASAN_POISON_MEMORY_REGION(addr, size)   __asan_poison_memory_region((addr), (size))
#  define ASAN_UNPOISON_MEMORY_REGION(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#  define ASAN_POISON_MEMORY_REGION(addr, size)   ((void)(addr), (void)(size))
#  define ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#endif

Arena arena_init(CB_u8 *backing, CB_size capacity)
{
  CB_assert(backing);
  CB_assert(capacity > 0);
  Arena result = {0};
  result.at = result.backing = backing;
  result.capacity = capacity;
  ASAN_POISON_MEMORY_REGION(backing, (CB_usize)capacity);
  return result;
}

__attribute__((malloc, alloc_size(2,4), alloc_align(3)))
CB_u8 *arena_alloc(Arena *a, CB_size objsize, CB_size align, CB_size count)
{
  CB_assert(a->at >= a->backing);
  CB_size avail = (a->backing + a->capacity) - a->at;
  CB_size padding = -(CB_size)((CB_uptr)a->at) & (align - 1);
  CB_size total   = padding + objsize * count;
  if (avail < total) {
    os_write(2, (CB_u8 *)"Out of Memory", 13);
    os_exit(1);
  }

  CB_u8 *p = a->at + padding;
  a->at += total;
  ASAN_UNPOISON_MEMORY_REGION(p, (CB_usize)(objsize * count));

  // TODO: memcpy
  for (CB_size i = 0; i < objsize * count; i++) {
    p[i] = 0;
  }

  return p;
}

Arena *alloc_arena(CB_size capacity)
{
  Arena *result = 0;
  CB_u8 *mem = os_malloc(capacity);
  Arena temp = arena_init(mem, capacity);
  result = new(&temp, Arena, 1);
  *result = temp;
  return result;
}

void free_arena(Arena *arena)
{
  CB_u8 *to_free = arena->backing;
  // TODO: memset here
  { arena->backing = arena->at = 0;
    arena->capacity = 0; }
  os_mfree(to_free);
}

Arena_Mark arena_push_mark(Arena *a)
{
  Arena_Mark result = {0};
  result.arena = a;
  result.marker = a->at;
  return result;
}

void arena_pop_mark(Arena_Mark a)
{
  CB_assert(a.arena->at > a.marker);
  CB_size len = a.arena->at - a.marker;
  CB_assert(len >= 0);
  ASAN_POISON_MEMORY_REGION(a.marker, (CB_usize)len);
  a.arena->at = a.marker;
}

static __thread Arena *g_thread_scratch_pool[SCRATCH_ARENA_COUNT] = {0, 0};

static Arena *arena_get_scratch_(Arena **conflicts, CB_size conflicts_len)
{
  CB_assert(conflicts_len < SCRATCH_ARENA_COUNT);

  Arena **scratch_pool = g_thread_scratch_pool;

  if (scratch_pool[0] == 0) {
    for (CB_size i = 0; i < SCRATCH_ARENA_COUNT; i++) {
      scratch_pool[i] = alloc_arena(SCRATCH_ARENA_CAPACITY);
    }
    return scratch_pool[0];
  }

  for (CB_size i = 0; i < SCRATCH_ARENA_COUNT; i++) {
    for (CB_size j = 0; j < conflicts_len; j++) {
      if (scratch_pool[i] == conflicts[j]) {
        break;
      }
      else {
        return scratch_pool[i];
      }
    }
  }

  if (conflicts_len == 0) {
    return scratch_pool[0];
  }
  CB_assert(0 && "unreachable if care was taken to provide conflicting arenas.");
  return 0;
}

Arena_Mark arena_get_scratch(Arena **conflicts, CB_size conflicts_len)
{
  Arena *a = arena_get_scratch_(conflicts, conflicts_len);
  Arena_Mark result = {0};
  if (a) {
   result = arena_push_mark(a);
  }
  return result;
}

void free_scratch_pool()
{
  for (CB_size i = 0; i < SCRATCH_ARENA_COUNT; i++) {
    Arena *a = g_thread_scratch_pool[i];
    if (a) {
      os_mfree(a->backing);
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
//- Buffered IO Implementation

Write_Buffer *mem_buffer(Arena *a, CB_size capacity)
{
  Write_Buffer *result = new(a, Write_Buffer, 1);
  result->buf = new(a, CB_u8, capacity);
  result->capacity = capacity;
  result->fd = -1;
  return result;
}

Write_Buffer *fd_buffer(CB_i32 fd, Arena *a, CB_size capacity)
{
  Write_Buffer *result = new(a, Write_Buffer, 1);
  result->buf = new(a, CB_u8, capacity);
  result->capacity = capacity;
  result->fd = fd;
  return result;
}

void append(Write_Buffer *b, unsigned char *src, CB_size len)
{
  CB_assert(b);
  unsigned char *end = src + len;
  while (!b->error && src < end) {
    CB_size left = (end - src);
    CB_size avail = b->capacity - b->len;
    CB_size amount = avail<left ? avail : left;

    for (CB_i32 i = 0; i < amount; i++) {
      b->buf[b->len+i] = src[i];
    }
    b->len += amount;
    src += amount;

    if (amount < left) {
      flush(b);
    }
  }
}

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
  unsigned char *end = tmp + CB_sizeof(tmp);
  unsigned char *beg = end;
  long t = x>0 ? -x : x;
  do {
    *--beg = '0' - (unsigned char)(t%10);
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
//- Dynamic Array Implemntation

void da_grow(Arena *arena, void **__restrict items, CB_size *__restrict capacity, CB_size *__restrict len, CB_size item_size, CB_size align)
{
  CB_assert(*items != 0 && *capacity > 0);
  CB_u8 *items_end = (((CB_u8*)(*items)) + (item_size * (*len)));
  if (arena->at == items_end) {
    // Extend in place, no allocation occured between da_grow calls
    arena_alloc(arena, item_size, align, (*capacity));
    *capacity *= 2;
  }
  else {
    // Relocate array
    CB_u8 *p = arena_alloc(arena, item_size, align, (*capacity) * 2);
    // TODO(hk): memcpy
#define DA_MEMORY_COPY(dst, src, bytes) do { for (int i = 0; i < bytes; i++) { ((char *)dst)[i] = ((char *)src)[i]; } } while(0)
    DA_MEMORY_COPY(p, *items, (*len) * item_size);
#undef DA_MEMORY_COPY
    *items = (void *)p;
    *capacity *= 2;
  }
}


////////////////////////////////////////////////////////////////////////////////
//- Log Implementation

void log_begin(Write_Buffer *b, Log_Level level, Str prefix) {
  CB_assert(LOG_COUNT == 3 && "implement me");
  switch (level) {
  case LOG_ERROR: {
    append_lit(b, "[ERROR]: ");
  } break;
  case LOG_WARNING: {
    append_lit(b, "[WARNING]: ");
  } break;
  case LOG_INFO: {
    append_lit(b, "[INFO]: ");
  } break;
  default:
    CB_assert(0 && "unreachable");
  }
  if (prefix.buf) append_str(b, prefix);
}
void log_end(Write_Buffer *b, Str suffix) {
  if (suffix.buf) append_str(b, suffix);
  append_byte(b, '\n');
  flush(b);
}
void log_emit(Write_Buffer *b, Log_Level level, Str fmt) {
  log_begin(b, level, fmt);
  log_end(b, (Str){0});
}


////////////////////////////////////////////////////////////////////////////////
//- Command Implementation

void cmd_append(Arena *arena, Command *cmd, Str arg) {
  *(da_push(arena, cmd)) = arg;
}

void cmd_append_lits_(Arena *arena, Command *cmd, const char *lits[],
                      CB_size lits_len) {
  for (CB_size i = 0; i < lits_len; i++) {
    Str str = str_from_cstr((char *)lits[i]);
    *(da_push(arena, cmd)) = str;
  }
}

Str cmd_render(Command cmd, Write_Buffer *buf) {
  Str result = {0};
  result.len = buf->len;
  result.buf = buf->buf + buf->len;
  for (CB_size i = 0; i < cmd.len; i++) {
    if (i > 0) {
      append_byte(buf, ' ');
    }
    append_str(buf, cmd.items[i]);
  }
  result.len = buf->len - result.len;
  return result;
}


////////////////////////////////////////////////////////////////////////////////
//- Platform Implemntation

#ifdef _WIN32
//-- Windows Platform
#error "windows api not implemented."
#else // Linux
//-- Linux/Posix Platform

#include <stdlib.h> // malloc
#include <stdio.h> // rename
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>

CB_u8 *os_malloc(CB_size amount)
{
  CB_u8 *mem = (CB_u8 *)malloc((CB_usize)amount);
  if (mem == 0) {
    os_write(2, (CB_u8 *)"Out of memory\n", 15);
    os_exit(1);
  }
  return mem;
}

void os_mfree(CB_u8 *memory_to_free)
{
  free(memory_to_free);
}

CB_i32  os_open(Str filepath, Write_Buffer *stderr)
{
  Arena_Mark scratch = arena_get_scratch(0, 0);
  CB_i32 result = 0;

  char *c_filepath = str_to_cstr(scratch.arena, filepath);
  CB_i32 fd = open(c_filepath, O_RDWR | O_CREAT | O_TRUNC, 0755);
  if (fd < 0) {
    log_begin(stderr, LOG_ERROR, S("Could not open file "));
      append_str(stderr, filepath);
      append_lit(stderr, ": ");
      append_str(stderr, str_from_cstr(strerror(errno)));
    log_end(stderr, (Str){0});
    cb_return_defer(0);
  }
  cb_return_defer(fd);

 defer:
  arena_pop_mark(scratch);
  return result;
}

CB_b32 os_close(CB_i32 fd, Write_Buffer *stderr)
{
  CB_i32 status = close(fd);
  if (status < 0) {
    log_begin(stderr, LOG_ERROR, S("Could not close file (fd "));
      append_long(stderr, fd);
      append_lit(stderr, "): ");
      append_str(stderr, str_from_cstr(strerror(errno)));
    log_end(stderr, (Str){0});
    return 0;
  }
  return 1;
}

CB_b32 os_file_exists(Str filepath, Write_Buffer *stderr)
{
  Arena_Mark scratch = arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_filepath = str_to_cstr(scratch.arena, filepath);
  struct stat statbuf = {0};
  if (stat(c_filepath, &statbuf) < 0) {
    if (errno == ENOENT) { cb_return_defer(0); }
    log_begin(stderr, LOG_ERROR, S("Could not stat "));
      append_str(stderr, filepath);
      append_lit(stderr, ": ");
      append_str(stderr, str_from_cstr(strerror(errno)));
    log_end(stderr, (Str){0});
    cb_return_defer(-1);
  }
  cb_return_defer(1);

 defer:
  arena_pop_mark(scratch);
  return result;
}

CB_b32 os_write(CB_i32 fd, CB_u8 *buf, CB_size len)
{
  for (CB_size off = 0; off < len;) {
    CB_usize bytes_to_write = (CB_usize)(len - off);
    CB_size written = write(fd, buf + off, bytes_to_write);
    if (written < 1) { return 0; }
    off += written;
  }
  return 1;
}

void os_exit(int status)
{
  exit(status);
}

CB_b32 os_mkdir_if_not_exists(Str directory, Write_Buffer *stderr)
{
  Arena_Mark scratch = arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_directory = str_to_cstr(scratch.arena, directory);
  int ok = mkdir(c_directory, 0755);
  if (ok < 0) {
    if (errno == EEXIST) {
      cb_return_defer(1);
    }

    log_begin(stderr, LOG_ERROR, S("Could not create directory \""));
      append_str(stderr, directory);
    log_end(stderr, S("\""));
    cb_return_defer(0);
  }

  result = 1;
  log_begin(stderr, LOG_INFO, S("Created directory \""));
    append_str(stderr, directory);
  log_end(stderr, S("\""));

 defer:
  arena_pop_mark(scratch);
  return result;
}

CB_b32 os_rename(Str old_path, Str new_path, Write_Buffer *stderr)
{
  Arena_Mark scratch = arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_old_path = str_to_cstr(scratch.arena, old_path);
  char *c_new_path = str_to_cstr(scratch.arena, new_path);
  if (rename(c_old_path, c_new_path) < 0) {
    log_begin(stderr, LOG_ERROR, S("Could not rename "));
      append_str(stderr, old_path);
      append_lit(stderr, " to ");
      append_str(stderr, new_path);
      append_lit(stderr, ": ");
      append_str(stderr, str_from_cstr(strerror(errno)));
    log_end(stderr, (Str){0});
    cb_return_defer(0);
  }
  cb_return_defer(1);

 defer:
  arena_pop_mark(scratch);
  return result;
}

CB_b32 os_needs_rebuild(Str output_path, Str *input_paths, int input_paths_len, Write_Buffer *stderr)
{
  Arena_Mark scratch = arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_output_path = str_to_cstr(scratch.arena, output_path);

  struct stat statbuf = {0};
  if (stat(c_output_path, &statbuf) < 0) {
    // NOTE: if output does not exist it 100% must be rebuilt
    if (errno == ENOENT) { cb_return_defer(1); }
    log_begin(stderr, LOG_ERROR, S("Could not stat "));
      append_str(stderr, output_path);
      append_lit(stderr, ": ");
      append_str(stderr, str_from_cstr(strerror(errno)));
    log_end(stderr, (Str){0});
    cb_return_defer(-1);
  }
  CB_i64 output_path_time = statbuf.st_mtime;

  for (CB_size i = 0; i < input_paths_len; ++i) {
    Str input_path = input_paths[i];
    char *c_input_path = str_to_cstr(scratch.arena, input_path);
    if (stat(c_input_path, &statbuf) < 0) {
      // NOTE: non-existing input is an error cause it is needed for building in the first place
      log_begin(stderr, LOG_ERROR, S("Could not stat "));
        append_str(stderr, input_path);
        append_lit(stderr, ": ");
        append_str(stderr, str_from_cstr(strerror(errno)));
      log_end(stderr, S(""));
      cb_return_defer(-1);
    }
    CB_i64 input_path_time = statbuf.st_mtime;
    // NOTE: if even a single input_path is fresher than output_path that's 100% rebuild
    if (input_path_time > output_path_time) cb_return_defer(1);
  }

 defer:
  arena_pop_mark(scratch);
  return result;
}

int os_run_cmd_async(Command command, Write_Buffer *stderr)
{
  CB_assert(command.len >= 1);

  log_begin(stderr, LOG_INFO, S("CMD: "));
    cmd_render(command, stderr);
  log_end(stderr, (Str){0});

  pid_t cpid = fork();
  if (cpid < 0) {
    log_begin(stderr, LOG_ERROR, S("Could not fork child process: "));
      append_str(stderr, str_from_cstr(strerror(errno)));
    log_end(stderr, (Str){0});
    return OS_INVALID_PROC;
  }

  if (cpid == 0) {
    Arena_Mark scratch = arena_get_scratch(0, 0); // REVIEW: not sure what happens here, we never pop the mark
    char *cmd_null[512];
    { // Fill cmd
      CB_size i = 0;
      for (i = 0; i < command.len; i++) {
        cmd_null[i] = str_to_cstr(scratch.arena, command.items[i]);
      }
      CB_assert(i < 512);
      cmd_null[i] = 0;
    }

    if (execvp(cmd_null[0], cmd_null) < 0) {
      log_begin(stderr, LOG_ERROR, S("Could not exec child process: "));
        append_str(stderr, str_from_cstr(strerror(errno)));
      log_end(stderr, (Str){0});
      os_exit(1);
    }
    CB_assert(0 && "unreachable");
  }

  return cpid;
}

CB_b32 os_run_cmd_sync(Command command, Write_Buffer *stderr)
{
  OS_Proc proc = os_run_cmd_async(command, stderr);
  if (!os_proc_wait(proc, stderr)) {
    return 0;
  }
  return 1;
}

CB_b32 os_proc_wait(OS_Proc proc, Write_Buffer *stderr)
{
  if (proc == OS_INVALID_PROC) return 0;

  for (;;) {
    int wstatus = 0;
    if (waitpid(proc, &wstatus, 0) < 0) {
      log_begin(stderr, LOG_ERROR, S("Could not wait on child process (pid "));
        append_long(stderr, (long)proc);
        append_lit(stderr, "): ");
        append_str(stderr, str_from_cstr(strerror(errno)));
      log_end(stderr, (Str){0});
      return 0;
    }

    if (WIFEXITED(wstatus)) {
      int exit_status = WEXITSTATUS(wstatus);
      if (exit_status != 0) {
        log_begin(stderr, LOG_ERROR, S("Child process exited with exit code "));
          append_long(stderr, (long)exit_status);
        log_end(stderr, (Str){0});
        return 0;
      }

      break;
    }

    if (WIFSIGNALED(wstatus)) {
      log_begin(stderr, LOG_ERROR, S("Child process was terminated by "));
        append_str(stderr, str_from_cstr(strsignal(WTERMSIG(wstatus))));
      log_end(stderr, (Str){0});
      return 0;
    }
  }

  return 1;
}


#endif // __LINUX__

#endif // CBUILD_IMPLEMENTATION
