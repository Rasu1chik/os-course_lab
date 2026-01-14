#ifndef __APPLE__
#define _GNU_SOURCE
#endif

#include "vtpc.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef VTPC_BLOCK_SIZE
#define VTPC_BLOCK_SIZE 4096u
#endif

#ifndef VTPC_CACHE_PAGES
#define VTPC_CACHE_PAGES 256u
#endif

#ifndef VTPC_MAX_OPEN_FILES
#define VTPC_MAX_OPEN_FILES 64
#endif

#ifndef VTPC_ADVICE_SLOTS
#define VTPC_ADVICE_SLOTS 2048
#endif

typedef struct {
  off_t block_no;
  uint8_t* data;
  int valid;
  int dirty;
  uint64_t next_use;  // Optimal: max next_use is evicted; UINT64_MAX = "∞"
} cache_page_t;

typedef struct {
  int used;
  int os_fd;
  off_t pos;
  off_t file_size;

  cache_page_t pages[VTPC_CACHE_PAGES];

  struct {
    int valid;
    off_t block_no;
    uint64_t next_use;
  } advice[VTPC_ADVICE_SLOTS];

  struct {
    int can_write;

    uint64_t logical_reads;
    uint64_t logical_writes;

    uint64_t read_hits;
    uint64_t read_misses;

    uint64_t write_hits;
    uint64_t write_misses;

    uint64_t disk_reads;
    uint64_t disk_writes;
    uint64_t disk_read_bytes;
    uint64_t disk_write_bytes;

    uint64_t evictions;
    uint64_t dirty_evictions;

    uint64_t fsync_calls;
    uint64_t max_dirty_pages;
  } stats;
} vtpc_file_t;

static vtpc_file_t g_files[VTPC_MAX_OPEN_FILES];

/* ---------- helpers ---------- */

static int get_file_size(int os_fd, off_t* out) {
  struct stat st;
  if (fstat(os_fd, &st) != 0)
    return -1;
  *out = st.st_size;
  return 0;
}

static int fd_to_idx(int fd) {
  int idx = fd - 3;
  if (idx < 0 || idx >= VTPC_MAX_OPEN_FILES)
    return -1;
  if (!g_files[idx].used)
    return -1;
  return idx;
}

static void compute_block(off_t pos, off_t* block_no, size_t* in_block) {
  *block_no = pos / (off_t)VTPC_BLOCK_SIZE;
  *in_block = (size_t)(pos % (off_t)VTPC_BLOCK_SIZE);
}

static uint64_t count_dirty_pages(vtpc_file_t* f) {
  uint64_t c = 0;
  for (size_t i = 0; i < VTPC_CACHE_PAGES; i++) {
    if (f->pages[i].valid && f->pages[i].dirty)
      c++;
  }
  return c;
}

/* ---------- Optimal advice ---------- */

static void advice_set(vtpc_file_t* f, off_t block_no, uint64_t next_use) {
  for (size_t i = 0; i < VTPC_ADVICE_SLOTS; i++) {
    if (f->advice[i].valid && f->advice[i].block_no == block_no) {
      f->advice[i].next_use = next_use;
      return;
    }
  }
  for (size_t i = 0; i < VTPC_ADVICE_SLOTS; i++) {
    if (!f->advice[i].valid) {
      f->advice[i].valid = 1;
      f->advice[i].block_no = block_no;
      f->advice[i].next_use = next_use;
      return;
    }
  }
  f->advice[VTPC_ADVICE_SLOTS - 1].valid = 1;
  f->advice[VTPC_ADVICE_SLOTS - 1].block_no = block_no;
  f->advice[VTPC_ADVICE_SLOTS - 1].next_use = next_use;
}

static uint64_t advice_get(vtpc_file_t* f, off_t block_no) {
  for (size_t i = 0; i < VTPC_ADVICE_SLOTS; i++) {
    if (f->advice[i].valid && f->advice[i].block_no == block_no)
      return f->advice[i].next_use;
  }
  return UINT64_MAX;
}

/* ---------- disk I/O ---------- */

static int disk_read_block(int fd, uint8_t* dst, off_t block_no, off_t file_size) {
  off_t off = block_no * (off_t)VTPC_BLOCK_SIZE;

  if (off >= file_size) {
    memset(dst, 0, VTPC_BLOCK_SIZE);
    return 0;
  }

  ssize_t r = pread(fd, dst, VTPC_BLOCK_SIZE, off);
  if (r < 0)
    return -1;

  if ((size_t)r < VTPC_BLOCK_SIZE)
    memset(dst + r, 0, VTPC_BLOCK_SIZE - (size_t)r);

  return 0;
}

static int disk_write_block(int fd, const uint8_t* src, off_t block_no) {
  off_t off = block_no * (off_t)VTPC_BLOCK_SIZE;

  ssize_t w = pwrite(fd, src, VTPC_BLOCK_SIZE, off);
  if (w < 0)
    return -1;
  if ((size_t)w != VTPC_BLOCK_SIZE) {
    errno = EIO;
    return -1;
  }
  return 0;
}

/* ---------- cache ---------- */

static cache_page_t* cache_find(vtpc_file_t* f, off_t block_no) {
  for (size_t i = 0; i < VTPC_CACHE_PAGES; i++) {
    if (f->pages[i].valid && f->pages[i].block_no == block_no)
      return &f->pages[i];
  }
  return NULL;
}

static cache_page_t* cache_find_free(vtpc_file_t* f) {
  for (size_t i = 0; i < VTPC_CACHE_PAGES; i++) {
    if (!f->pages[i].valid)
      return &f->pages[i];
  }
  return NULL;
}

static cache_page_t* cache_choose_victim_optimal(vtpc_file_t* f) {
  cache_page_t* victim = NULL;
  uint64_t best = 0;

  for (size_t i = 0; i < VTPC_CACHE_PAGES; i++) {
    cache_page_t* p = &f->pages[i];
    if (!p->valid)
      continue;
    if (!victim || p->next_use >= best) {
      best = p->next_use;
      victim = p;
    }
  }
  return victim;
}

static cache_page_t* cache_get_or_load(vtpc_file_t* f, off_t block_no) {
  cache_page_t* p = cache_find(f, block_no);
  if (p)
    return p;

  p = cache_find_free(f);
  if (!p) {
    p = cache_choose_victim_optimal(f);
    if (!p) {
      errno = ENOMEM;
      return NULL;
    }

    f->stats.evictions++;
    if (p->dirty)
      f->stats.dirty_evictions++;

    if (p->dirty) {
      if (disk_write_block(f->os_fd, p->data, p->block_no) != 0)
        return NULL;

      f->stats.disk_writes++;
      f->stats.disk_write_bytes += VTPC_BLOCK_SIZE;

      p->dirty = 0;
    }
  }

  if (disk_read_block(f->os_fd, p->data, block_no, f->file_size) != 0)
    return NULL;

  f->stats.disk_reads++;
  f->stats.disk_read_bytes += VTPC_BLOCK_SIZE;

  p->valid = 1;
  p->dirty = 0;
  p->block_no = block_no;
  p->next_use = advice_get(f, block_no);
  return p;
}

static int cache_flush_all(vtpc_file_t* f) {
  for (size_t i = 0; i < VTPC_CACHE_PAGES; i++) {
    cache_page_t* p = &f->pages[i];
    if (p->valid && p->dirty) {
      if (disk_write_block(f->os_fd, p->data, p->block_no) != 0)
        return -1;

      f->stats.disk_writes++;
      f->stats.disk_write_bytes += VTPC_BLOCK_SIZE;

      p->dirty = 0;
    }
  }

  // ВАЖНО: ftruncate только если файл реально открыт на запись.
  if (f->stats.can_write) {
    if (ftruncate(f->os_fd, f->file_size) != 0)
      return -1;
  }

  return 0;
}

/* ---------- API ---------- */

int vtpc_open(const char* path, int mode, int access) {
  if (!path) {
    errno = EINVAL;
    return -1;
  }

  int idx = -1;
  for (int i = 0; i < VTPC_MAX_OPEN_FILES; i++) {
    if (!g_files[i].used) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    errno = EMFILE;
    return -1;
  }

  int os_fd = open(path, mode, access);
  if (os_fd < 0)
    return -1;

#ifdef __APPLE__
  // Отключаем системный page cache на macOS
  if (fcntl(os_fd, F_NOCACHE, 1) == -1) {
    int e = errno;
    close(os_fd);
    errno = e;
    return -1;
  }
#endif

  off_t sz = 0;
  if (get_file_size(os_fd, &sz) != 0) {
    int e = errno;
    close(os_fd);
    errno = e;
    return -1;
  }

  vtpc_file_t* f = &g_files[idx];
  memset(f, 0, sizeof(*f));
  f->used = 1;
  f->os_fd = os_fd;
  f->pos = 0;
  f->file_size = sz;

  int acc = (mode & O_ACCMODE);
  f->stats.can_write = (acc != O_RDONLY);

  for (size_t i = 0; i < VTPC_CACHE_PAGES; i++) {
    void* mem = NULL;
    int rc = posix_memalign(&mem, VTPC_BLOCK_SIZE, VTPC_BLOCK_SIZE);
    if (rc != 0) {
      int saved = errno;
      for (size_t j = 0; j < i; j++)
        free(f->pages[j].data);
      close(os_fd);
      memset(f, 0, sizeof(*f));
      errno = saved;
      return -1;
    }

    f->pages[i].data = (uint8_t*)mem;
    f->pages[i].valid = 0;
    f->pages[i].dirty = 0;
    f->pages[i].next_use = UINT64_MAX;
    memset(f->pages[i].data, 0, VTPC_BLOCK_SIZE);
  }

  return idx + 3;
}

int vtpc_close(int fd) {
  int idx = fd_to_idx(fd);
  if (idx < 0) {
    errno = EBADF;
    return -1;
  }

  vtpc_file_t* f = &g_files[idx];

  if (vtpc_fsync(fd) != 0)
    return -1;

  int rc = close(f->os_fd);
  int saved = errno;

  for (size_t i = 0; i < VTPC_CACHE_PAGES; i++)
    free(f->pages[i].data);

  memset(f, 0, sizeof(*f));

  errno = saved;
  return rc;
}

ssize_t vtpc_read(int fd, void* buf, size_t count) {
  int idx = fd_to_idx(fd);
  if (idx < 0) {
    errno = EBADF;
    return -1;
  }
  if (!buf && count != 0) {
    errno = EINVAL;
    return -1;
  }

  vtpc_file_t* f = &g_files[idx];
  f->stats.logical_reads++;

  if (f->pos >= f->file_size)
    return 0;

  if (f->pos + (off_t)count > f->file_size)
    count = (size_t)(f->file_size - f->pos);

  uint8_t* out = (uint8_t*)buf;
  size_t done = 0;

  while (done < count) {
    off_t block_no;
    size_t in_block;
    compute_block(f->pos, &block_no, &in_block);

    size_t can = VTPC_BLOCK_SIZE - in_block;
    size_t need = count - done;
    size_t take = can < need ? can : need;

    if (cache_find(f, block_no))
      f->stats.read_hits++;
    else
      f->stats.read_misses++;

    cache_page_t* p = cache_get_or_load(f, block_no);
    if (!p)
      return -1;

    p->next_use = advice_get(f, block_no);
    memcpy(out + done, p->data + in_block, take);

    f->pos += (off_t)take;
    done += take;
  }

  return (ssize_t)done;
}

ssize_t vtpc_write(int fd, const void* buf, size_t count) {
  int idx = fd_to_idx(fd);
  if (idx < 0) {
    errno = EBADF;
    return -1;
  }
  if (!buf && count != 0) {
    errno = EINVAL;
    return -1;
  }

  vtpc_file_t* f = &g_files[idx];
  f->stats.logical_writes++;

  const uint8_t* in = (const uint8_t*)buf;
  size_t done = 0;

  while (done < count) {
    off_t block_no;
    size_t in_block;
    compute_block(f->pos, &block_no, &in_block);

    size_t can = VTPC_BLOCK_SIZE - in_block;
    size_t need = count - done;
    size_t take = can < need ? can : need;

    if (cache_find(f, block_no))
      f->stats.write_hits++;
    else
      f->stats.write_misses++;

    cache_page_t* p = cache_get_or_load(f, block_no);
    if (!p)
      return -1;

    p->next_use = advice_get(f, block_no);
    memcpy(p->data + in_block, in + done, take);
    p->dirty = 1;

    uint64_t d = count_dirty_pages(f);
    if (d > f->stats.max_dirty_pages)
      f->stats.max_dirty_pages = d;

    f->pos += (off_t)take;
    done += take;

    if (f->pos > f->file_size)
      f->file_size = f->pos;
  }

  return (ssize_t)done;
}

off_t vtpc_lseek(int fd, off_t offset, int whence) {
  int idx = fd_to_idx(fd);
  if (idx < 0) {
    errno = EBADF;
    return (off_t)-1;
  }

  if (whence != SEEK_SET || offset < 0) {
    errno = EINVAL;
    return (off_t)-1;
  }

  g_files[idx].pos = offset;
  return offset;
}

int vtpc_fsync(int fd) {
  int idx = fd_to_idx(fd);
  if (idx < 0) {
    errno = EBADF;
    return -1;
  }

  vtpc_file_t* f = &g_files[idx];
  f->stats.fsync_calls++;

  if (cache_flush_all(f) != 0)
    return -1;

  if (fsync(f->os_fd) != 0)
    return -1;

  return 0;
}

int vtpc_advice(int fd, off_t offset, uint64_t next_use) {
  int idx = fd_to_idx(fd);
  if (idx < 0) {
    errno = EBADF;
    return -1;
  }
  if (offset < 0) {
    errno = EINVAL;
    return -1;
  }

  vtpc_file_t* f = &g_files[idx];
  off_t block_no = offset / (off_t)VTPC_BLOCK_SIZE;

  advice_set(f, block_no, next_use);

  cache_page_t* p = cache_find(f, block_no);
  if (p)
    p->next_use = next_use;

  return 0;
}

void vtpc_print_stats(int fd) {
  int idx = fd_to_idx(fd);
  if (idx < 0)
    return;

  vtpc_file_t* f = &g_files[idx];

  uint64_t rtot = f->stats.read_hits + f->stats.read_misses;
  uint64_t wtot = f->stats.write_hits + f->stats.write_misses;

  double rratio = rtot ? (double)f->stats.read_hits / (double)rtot : 0.0;
  double wratio = wtot ? (double)f->stats.write_hits / (double)wtot : 0.0;

  printf("VTPC statistics:\n");
  printf("Logical reads:  %" PRIu64 "\n", f->stats.logical_reads);
  printf("Logical writes: %" PRIu64 "\n", f->stats.logical_writes);

  printf("Reads:  hits=%" PRIu64 " misses=%" PRIu64 " hit_ratio=%.3f\n",
         f->stats.read_hits, f->stats.read_misses, rratio);

  printf("Writes: hits=%" PRIu64 " misses=%" PRIu64 " hit_ratio=%.3f\n",
         f->stats.write_hits, f->stats.write_misses, wratio);

  printf("Disk reads:  %" PRIu64 " (bytes=%" PRIu64 ")\n",
         f->stats.disk_reads, f->stats.disk_read_bytes);

  printf("Disk writes: %" PRIu64 " (bytes=%" PRIu64 ")\n",
         f->stats.disk_writes, f->stats.disk_write_bytes);

  printf("Evictions: %" PRIu64 " (dirty=%" PRIu64 ")\n",
         f->stats.evictions, f->stats.dirty_evictions);

  printf("fsync calls: %" PRIu64 "\n", f->stats.fsync_calls);
  printf("max dirty pages: %" PRIu64 "\n", f->stats.max_dirty_pages);
}