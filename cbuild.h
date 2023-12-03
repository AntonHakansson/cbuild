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

#define new(a, t, n) (t *) cb_arena_alloc(a, CB_sizeof(t), CB_alignof(t), (n))

typedef struct {
  CB_u8 *backing;
  CB_u8 *at;
  CB_size capacity;
} CB_Arena;

CB_Arena *cb_alloc_arena(CB_size capacity);
void   cb_free_arena(CB_Arena *arena);

CB_Arena cb_arena_init(CB_u8 *backing, CB_size capacity);
__attribute__((malloc, alloc_size(2,4), alloc_align(3)))
CB_u8 *cb_arena_alloc(CB_Arena *a, CB_size objsize, CB_size align, CB_size count);

typedef struct {
  CB_Arena *arena;
  CB_u8 *marker;
} CB_Arena_Mark;

CB_Arena_Mark cb_arena_push_mark(CB_Arena *a);
void cb_arena_pop_mark(CB_Arena_Mark a);

#define SCRATCH_ARENA_COUNT 2
#define SCRATCH_ARENA_CAPACITY (8 * 1024 * 1024)

CB_Arena_Mark cb_arena_scratch(CB_Arena **conflicts, CB_size conflicts_len);
void cb_free_scratch_pool();


////////////////////////////////////////////////////////////////////////////////
//- String slice

#define S(s)        (CB_Str){ .buf = (CB_u8 *)(s), .len = CB_countof((s)) - 1, }
#define S_FMT       "%.*s"
#define S_ARG(s)    (i32)(s).len, (s).buf

typedef struct {
  CB_u8 *buf;
  CB_size len;
} CB_Str;

CB_Str cb_str_from_cstr(char *str);
CB_b32 cb_str_equals(CB_Str a, CB_Str b);
char *cb_str_to_cstr(CB_Arena *a, CB_Str s);


////////////////////////////////////////////////////////////////////////////////
//- Buffered IO

typedef struct  {
  CB_u8 *buf;
  CB_size capacity;
  CB_size len;
  CB_i32 fd;
  CB_b32 error;
} CB_Write_Buffer;

CB_Write_Buffer *cb_mem_buffer(CB_Arena *a, CB_size capacity);
CB_Write_Buffer *cb_fd_buffer(CB_i32 fd, CB_Arena *a, CB_size capacity);
void cb_flush(CB_Write_Buffer *b);

void    cb_append(CB_Write_Buffer *b, unsigned char *src, CB_size len);
#define cb_append_lit(b, s) cb_append(b, (unsigned char*)s, CB_sizeof(s) - 1)
void    cb_append_str(CB_Write_Buffer *b, CB_Str s);
void    cb_append_byte(CB_Write_Buffer *b, unsigned char c);
void    cb_append_long(CB_Write_Buffer *b, long x);


////////////////////////////////////////////////////////////////////////////////
//- Dynamic Array

#define cb_da_init(a, t, cap) ({                                        \
      t s = {0};                                                        \
      s.capacity = cap;                                                 \
      s.items = (typeof(s.items))                                       \
        cb_arena_alloc((a),                                             \
                    CB_sizeof(s.items[0]),                              \
                    CB_alignof(typeof(s.items[0])),                     \
                    s.capacity);                                        \
      s;                                                                \
})

// Allocates on demand
#define cb_push(a, s) ({                                                \
      typeof(s) _s = s;                                                 \
      if (_s->len >= _s->capacity) {                                    \
        cb_da_grow((a), (void **)&_s->items, &_s->capacity, &_s->len,   \
                   CB_sizeof(_s->items[0]),                             \
                   CB_alignof(typeof(_s->items[0])));                   \
      }                                                                 \
      _s->items + _s->len++;                                            \
    })

// Assumes new item fits in capacity
#define da_push_unsafe(s) ({                       \
      CB_assert((s)->len < (s)->capacity);         \
      (s)->items + (s)->len++;                     \
    })

void cb_da_grow(CB_Arena *arena, void **items, CB_size *__restrict capacity, CB_size *__restrict len, CB_size item_size, CB_size align);


////////////////////////////////////////////////////////////////////////////////
//- Log

typedef enum CB_Log_Level {
  CB_LOG_ERROR,
  CB_LOG_WARNING,
  CB_LOG_INFO,

  CB_LOG_COUNT,
} CB_Log_Level;

void cb_log_begin(CB_Write_Buffer *b, CB_Log_Level level, CB_Str prefix);
void cb_log_end(CB_Write_Buffer *b, CB_Str suffix);
void cb_log_emit(CB_Write_Buffer *b, CB_Log_Level level, CB_Str fmt);


////////////////////////////////////////////////////////////////////////////////
//- Command

typedef struct CB_Command {
  CB_Str *items;
  CB_size capacity;
  CB_size len;
} CB_Command;

CB_Str  cb_cmd_render(CB_Command cmd, CB_Write_Buffer *buf);
void cb_cmd_append(CB_Arena *arena, CB_Command *cmd, CB_Str arg);
void cb_cmd_append_lits_(CB_Arena *arena, CB_Command *cmd, const char *lits[],
                      CB_size lits_len);
#define cb_cmd_append_lits(arena, cmd, ...) \
  cb_cmd_append_lits_(arena, cmd, ((const char*[]){__VA_ARGS__}), (CB_sizeof(((const char*[]){__VA_ARGS__}))/(CB_sizeof(const char*))))


////////////////////////////////////////////////////////////////////////////////
//- Platform

CB_u8  *cb_malloc(CB_size amount);
void cb_mfree(CB_u8 *memory_to_free);
__attribute__((noreturn)) void cb_exit (CB_i32 status);
CB_b32  cb_write(CB_i32 fd, CB_u8 *buf, CB_size len);
CB_i32  cb_open(CB_Str filepath, CB_Write_Buffer *stderr);
CB_b32  cb_close(CB_i32 fd, CB_Write_Buffer *stderr);
CB_b32  cb_file_exists(CB_Str filepath, CB_Write_Buffer *stderr);
CB_b32  cb_mkdir_if_not_exists(CB_Str directory, CB_Write_Buffer *stderr);
CB_b32  cb_rename(CB_Str old_path, CB_Str new_path, CB_Write_Buffer *stderr);
CB_b32  cb_needs_rebuild(CB_Str output_path, CB_Str *input_paths, int input_paths_len, CB_Write_Buffer *stderr);

#define CB_INVALID_PROC (-1)
typedef int CB_Proc;
int cb_cmd_run_async(CB_Command command, CB_Write_Buffer *stderr);
CB_b32 cb_cmd_run_sync(CB_Command command, CB_Write_Buffer* stderr);
CB_b32 cb_proc_wait(CB_Proc proc, CB_Write_Buffer *stderr);




#ifdef CBUILD_IMPLEMENTATION

///////////////////////////////////////////////////////////////////////////////
//- String slice Implemntation

CB_Str cb_str_from_cstr(char *str)
{
  CB_assert(str);
  CB_Str result = {0};
  result.buf = (CB_u8 *)str;

  while (*str) {
    result.len++;
    str++;
  }

  return result;
}

CB_b32 str_equals(CB_Str a, CB_Str b)
{
  if (a.len != b.len) { return 0; }
  for (CB_size i = 0; i < a.len; i++) {
    if (a.buf[i] != b.buf[i]) { return 0; }
  }
  return 1;
}

char *cb_str_to_cstr(CB_Arena *a, CB_Str s)
{
  CB_Write_Buffer *b = cb_mem_buffer(a, s.len + 1);
  cb_append_str(b, s);
  cb_append_byte(b, '\0');
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

CB_Arena cb_arena_init(CB_u8 *backing, CB_size capacity)
{
  CB_assert(backing);
  CB_assert(capacity > 0);
  CB_Arena result = {0};
  result.at = result.backing = backing;
  result.capacity = capacity;
  ASAN_POISON_MEMORY_REGION(backing, (CB_usize)capacity);
  return result;
}

__attribute__((malloc, alloc_size(2,4), alloc_align(3)))
CB_u8 *cb_arena_alloc(CB_Arena *a, CB_size objsize, CB_size align, CB_size count)
{
  CB_assert(a->at >= a->backing);
  CB_size avail = (a->backing + a->capacity) - a->at;
  CB_size padding = -(CB_size)((CB_uptr)a->at) & (align - 1);
  CB_size total   = padding + objsize * count;
  if (avail < total) {
    cb_write(2, (CB_u8 *)"Out of Memory", 13);
    cb_exit(1);
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

CB_Arena *cb_alloc_arena(CB_size capacity)
{
  CB_Arena *result = 0;
  CB_u8 *mem = cb_malloc(capacity);
  CB_Arena temp = cb_arena_init(mem, capacity);
  result = new(&temp, CB_Arena, 1);
  *result = temp;
  return result;
}

void cb_free_arena(CB_Arena *arena)
{
  CB_u8 *to_free = arena->backing;
  // TODO: memset here
  { arena->backing = arena->at = 0;
    arena->capacity = 0; }
  cb_mfree(to_free);
}

CB_Arena_Mark cb_arena_push_mark(CB_Arena *a)
{
  CB_Arena_Mark result = {0};
  result.arena = a;
  result.marker = a->at;
  return result;
}

void cb_arena_pop_mark(CB_Arena_Mark a)
{
  CB_assert(a.arena->at > a.marker);
  CB_size len = a.arena->at - a.marker;
  CB_assert(len >= 0);
  ASAN_POISON_MEMORY_REGION(a.marker, (CB_usize)len);
  a.arena->at = a.marker;
}

static __thread CB_Arena *g_thread_scratch_pool[SCRATCH_ARENA_COUNT] = {0, 0};

static CB_Arena *cb_arena_get_scratch_(CB_Arena **conflicts, CB_size conflicts_len)
{
  CB_assert(conflicts_len < SCRATCH_ARENA_COUNT);

  CB_Arena **scratch_pool = g_thread_scratch_pool;

  if (scratch_pool[0] == 0) {
    for (CB_size i = 0; i < SCRATCH_ARENA_COUNT; i++) {
      scratch_pool[i] = cb_alloc_arena(SCRATCH_ARENA_CAPACITY);
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

CB_Arena_Mark cb_arena_get_scratch(CB_Arena **conflicts, CB_size conflicts_len)
{
  CB_Arena *a = cb_arena_get_scratch_(conflicts, conflicts_len);
  CB_Arena_Mark result = {0};
  if (a) {
   result = cb_arena_push_mark(a);
  }
  return result;
}

void cb_free_scratch_pool()
{
  for (CB_size i = 0; i < SCRATCH_ARENA_COUNT; i++) {
    CB_Arena *a = g_thread_scratch_pool[i];
    if (a) {
      cb_mfree(a->backing);
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
//- Buffered IO Implementation

CB_Write_Buffer *cb_mem_buffer(CB_Arena *a, CB_size capacity)
{
  CB_Write_Buffer *result = new(a, CB_Write_Buffer, 1);
  result->buf = new(a, CB_u8, capacity);
  result->capacity = capacity;
  result->fd = -1;
  return result;
}

CB_Write_Buffer *cb_fd_buffer(CB_i32 fd, CB_Arena *a, CB_size capacity)
{
  CB_Write_Buffer *result = new(a, CB_Write_Buffer, 1);
  result->buf = new(a, CB_u8, capacity);
  result->capacity = capacity;
  result->fd = fd;
  return result;
}

void cb_append(CB_Write_Buffer *b, unsigned char *src, CB_size len)
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
      cb_flush(b);
    }
  }
}

void cb_append_str(CB_Write_Buffer *b, CB_Str s)
{
  cb_append(b, s.buf, s.len);
}

void cb_append_byte(CB_Write_Buffer *b, unsigned char c)
{
  cb_append(b, &c, 1);
}

void cb_append_long(CB_Write_Buffer *b, long x)
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
  cb_append(b, beg, end-beg);
}

void cb_flush(CB_Write_Buffer *b)
{
  b->error |= b->fd < 0;
  if (!b->error && b->len) {
    b->error |= !cb_write(b->fd, b->buf, b->len);
    b->len = 0;
  }
}


////////////////////////////////////////////////////////////////////////////////
//- Dynamic Array Implemntation

void cb_da_grow(CB_Arena *arena, void **__restrict items, CB_size *__restrict capacity, CB_size *__restrict len, CB_size item_size, CB_size align)
{
  CB_assert(*items != 0 && *capacity > 0);
  CB_u8 *items_end = (((CB_u8*)(*items)) + (item_size * (*len)));
  if (arena->at == items_end) {
    // Extend in place, no allocation occured between da_grow calls
    cb_arena_alloc(arena, item_size, align, (*capacity));
    *capacity *= 2;
  }
  else {
    // Relocate array
    CB_u8 *p = cb_arena_alloc(arena, item_size, align, (*capacity) * 2);
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

void cb_log_begin(CB_Write_Buffer *b, CB_Log_Level level, CB_Str prefix)
{
  CB_assert(CB_LOG_COUNT == 3 && "implement me");
  switch (level) {
  case CB_LOG_ERROR: {
    cb_append_lit(b, "[ERROR]: ");
  } break;
  case CB_LOG_WARNING: {
    cb_append_lit(b, "[WARNING]: ");
  } break;
  case CB_LOG_INFO: {
    cb_append_lit(b, "[INFO]: ");
  } break;
  default:
    CB_assert(0 && "unreachable");
  }
  if (prefix.buf) cb_append_str(b, prefix);
}

void cb_log_end(CB_Write_Buffer *b, CB_Str suffix)
{
  if (suffix.buf) cb_append_str(b, suffix);
  cb_append_byte(b, '\n');
  cb_flush(b);
}

void cb_log_emit(CB_Write_Buffer *b, CB_Log_Level level, CB_Str fmt)
{
  cb_log_begin(b, level, fmt);
  cb_log_end(b, (CB_Str){0});
}


////////////////////////////////////////////////////////////////////////////////
//- Command Implementation

void cb_cmd_append(CB_Arena *arena, CB_Command *cmd, CB_Str arg)
{
  *(cb_push(arena, cmd)) = arg;
}

void cb_cmd_append_lits_(CB_Arena *arena, CB_Command *cmd, const char *lits[],
                         CB_size lits_len)
{
  for (CB_size i = 0; i < lits_len; i++) {
    CB_Str str = cb_str_from_cstr((char *)lits[i]);
    *(cb_push(arena, cmd)) = str;
  }
}

CB_Str cb_cmd_render(CB_Command cmd, CB_Write_Buffer *buf)
{
  CB_Str result = {0};
  result.len = buf->len;
  result.buf = buf->buf + buf->len;
  for (CB_size i = 0; i < cmd.len; i++) {
    if (i > 0) {
      cb_append_byte(buf, ' ');
    }
    cb_append_str(buf, cmd.items[i]);
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

CB_u8 *cb_malloc(CB_size amount)
{
  CB_u8 *mem = (CB_u8 *)malloc((CB_usize)amount);
  if (mem == 0) {
    cb_write(2, (CB_u8 *)"Out of memory\n", 15);
    cb_exit(1);
  }
  return mem;
}

void cb_mfree(CB_u8 *memory_to_free)
{
  free(memory_to_free);
}

CB_i32  cb_open(CB_Str filepath, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_i32 result = 0;

  char *c_filepath = cb_str_to_cstr(scratch.arena, filepath);
  CB_i32 fd = open(c_filepath, O_RDWR | O_CREAT | O_TRUNC, 0755);
  if (fd < 0) {
    cb_log_begin(stderr, CB_LOG_ERROR, S("Could not open file "));
      cb_append_str(stderr, filepath);
      cb_append_lit(stderr, ": ");
      cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
    cb_log_end(stderr, (CB_Str){0});
    cb_return_defer(0);
  }
  cb_return_defer(fd);

 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

CB_b32 cb_close(CB_i32 fd, CB_Write_Buffer *stderr)
{
  CB_i32 status = close(fd);
  if (status < 0) {
    cb_log_begin(stderr, CB_LOG_ERROR, S("Could not close file (fd "));
      cb_append_long(stderr, fd);
      cb_append_lit(stderr, "): ");
      cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
    cb_log_end(stderr, (CB_Str){0});
    return 0;
  }
  return 1;
}

CB_b32 cb_file_exists(CB_Str filepath, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_filepath = cb_str_to_cstr(scratch.arena, filepath);
  struct stat statbuf = {0};
  if (stat(c_filepath, &statbuf) < 0) {
    if (errno == ENOENT) { cb_return_defer(0); }
    cb_log_begin(stderr, CB_LOG_ERROR, S("Could not stat "));
      cb_append_str(stderr, filepath);
      cb_append_lit(stderr, ": ");
      cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
    cb_log_end(stderr, (CB_Str){0});
    cb_return_defer(-1);
  }
  cb_return_defer(1);

 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

CB_b32 cb_write(CB_i32 fd, CB_u8 *buf, CB_size len)
{
  for (CB_size off = 0; off < len;) {
    CB_usize bytes_to_write = (CB_usize)(len - off);
    CB_size written = write(fd, buf + off, bytes_to_write);
    if (written < 1) { return 0; }
    off += written;
  }
  return 1;
}

void cb_exit(int status)
{
  exit(status);
}

CB_b32 cb_mkdir_if_not_exists(CB_Str directory, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_directory = cb_str_to_cstr(scratch.arena, directory);
  int ok = mkdir(c_directory, 0755);
  if (ok < 0) {
    if (errno == EEXIST) {
      cb_return_defer(1);
    }

    cb_log_begin(stderr, CB_LOG_ERROR, S("Could not create directory \""));
      cb_append_str(stderr, directory);
    cb_log_end(stderr, S("\""));
    cb_return_defer(0);
  }

  result = 1;
  cb_log_begin(stderr, CB_LOG_INFO, S("Created directory \""));
    cb_append_str(stderr, directory);
  cb_log_end(stderr, S("\""));

 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

CB_b32 cb_rename(CB_Str old_path, CB_Str new_path, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_old_path = cb_str_to_cstr(scratch.arena, old_path);
  char *c_new_path = cb_str_to_cstr(scratch.arena, new_path);
  if (rename(c_old_path, c_new_path) < 0) {
    cb_log_begin(stderr, CB_LOG_ERROR, S("Could not rename "));
      cb_append_str(stderr, old_path);
      cb_append_lit(stderr, " to ");
      cb_append_str(stderr, new_path);
      cb_append_lit(stderr, ": ");
      cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
    cb_log_end(stderr, (CB_Str){0});
    cb_return_defer(0);
  }
  cb_return_defer(1);

 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

CB_b32 cb_needs_rebuild(CB_Str output_path, CB_Str *input_paths, int input_paths_len, CB_Write_Buffer *stderr)
{
  CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0);
  CB_b32 result = 0;

  char *c_output_path = cb_str_to_cstr(scratch.arena, output_path);

  struct stat statbuf = {0};
  if (stat(c_output_path, &statbuf) < 0) {
    // NOTE: if output does not exist it 100% must be rebuilt
    if (errno == ENOENT) { cb_return_defer(1); }
    cb_log_begin(stderr, CB_LOG_ERROR, S("Could not stat "));
      cb_append_str(stderr, output_path);
      cb_append_lit(stderr, ": ");
      cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
    cb_log_end(stderr, (CB_Str){0});
    cb_return_defer(-1);
  }
  CB_i64 output_path_time = statbuf.st_mtime;

  for (CB_size i = 0; i < input_paths_len; ++i) {
    CB_Str input_path = input_paths[i];
    char *c_input_path = cb_str_to_cstr(scratch.arena, input_path);
    if (stat(c_input_path, &statbuf) < 0) {
      // NOTE: non-existing input is an error cause it is needed for building in the first place
      cb_log_begin(stderr, CB_LOG_ERROR, S("Could not stat "));
        cb_append_str(stderr, input_path);
        cb_append_lit(stderr, ": ");
        cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
      cb_log_end(stderr, S(""));
      cb_return_defer(-1);
    }
    CB_i64 input_path_time = statbuf.st_mtime;
    // NOTE: if even a single input_path is fresher than output_path that's 100% rebuild
    if (input_path_time > output_path_time) cb_return_defer(1);
  }

 defer:
  cb_arena_pop_mark(scratch);
  return result;
}

int cb_cmd_run_async(CB_Command command, CB_Write_Buffer *stderr)
{
  CB_assert(command.len >= 1);

  cb_log_begin(stderr, CB_LOG_INFO, S("CMD: "));
    cb_cmd_render(command, stderr);
  cb_log_end(stderr, (CB_Str){0});

  pid_t cpid = fork();
  if (cpid < 0) {
    cb_log_begin(stderr, CB_LOG_ERROR, S("Could not fork child process: "));
      cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
    cb_log_end(stderr, (CB_Str){0});
    return CB_INVALID_PROC;
  }

  if (cpid == 0) {
    CB_Arena_Mark scratch = cb_arena_get_scratch(0, 0); // REVIEW: not sure what happens here, we never pop the mark
    char *cmd_null[512];
    { // Fill cmd
      CB_size i = 0;
      for (i = 0; i < command.len; i++) {
        cmd_null[i] = cb_str_to_cstr(scratch.arena, command.items[i]);
      }
      CB_assert(i < 512);
      cmd_null[i] = 0;
    }

    if (execvp(cmd_null[0], cmd_null) < 0) {
      cb_log_begin(stderr, CB_LOG_ERROR, S("Could not exec child process: "));
        cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
      cb_log_end(stderr, (CB_Str){0});
      cb_exit(1);
    }
    CB_assert(0 && "unreachable");
  }

  return cpid;
}

CB_b32 cb_cmd_run_sync(CB_Command command, CB_Write_Buffer *stderr)
{
  CB_Proc proc = cb_cmd_run_async(command, stderr);
  if (!cb_proc_wait(proc, stderr)) {
    return 0;
  }
  return 1;
}

CB_b32 cb_proc_wait(CB_Proc proc, CB_Write_Buffer *stderr)
{
  if (proc == CB_INVALID_PROC) return 0;

  for (;;) {
    int wstatus = 0;
    if (waitpid(proc, &wstatus, 0) < 0) {
      cb_log_begin(stderr, CB_LOG_ERROR, S("Could not wait on child process (pid "));
        cb_append_long(stderr, (long)proc);
        cb_append_lit(stderr, "): ");
        cb_append_str(stderr, cb_str_from_cstr(strerror(errno)));
      cb_log_end(stderr, (CB_Str){0});
      return 0;
    }

    if (WIFEXITED(wstatus)) {
      int exit_status = WEXITSTATUS(wstatus);
      if (exit_status != 0) {
        cb_log_begin(stderr, CB_LOG_ERROR, S("Child process exited with exit code "));
          cb_append_long(stderr, (long)exit_status);
        cb_log_end(stderr, (CB_Str){0});
        return 0;
      }

      break;
    }

    if (WIFSIGNALED(wstatus)) {
      cb_log_begin(stderr, CB_LOG_ERROR, S("Child process was terminated by "));
        cb_append_str(stderr, cb_str_from_cstr(strsignal(WTERMSIG(wstatus))));
      cb_log_end(stderr, (CB_Str){0});
      return 0;
    }
  }

  return 1;
}


#endif // __LINUX__

#endif // CBUILD_IMPLEMENTATION
