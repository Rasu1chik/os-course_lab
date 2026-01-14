#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vtpc.h"

static void die(const char* msg) {
  perror(msg);
  exit(1);
}

static long diff_nsec(struct timespec a, struct timespec b) {
  long sec = b.tv_sec - a.tv_sec;
  long nsec = b.tv_nsec - a.tv_nsec;
  if (nsec < 0) {
    sec--;
    nsec += 1000000000L;
  }
  return sec * 1000000000L + nsec;
}

int main(int argc, char* argv[]) {
  const char* file_path = NULL;
  int block_size = 0;
  int block_count = 0;
  int range_start = 0;
  int random_mode = 0;
  int iterations = 1;

  // один запуск, который делает write+read
  int do_write = 1;
  int do_read = 1;

  // NEW: выбор кеша
  const char* cache = "vtpc";  // vtpc|ospc
  int use_vtpc = 1;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
      cache = argv[++i];
    } else if (strcmp(argv[i], "--block_size") == 0 && i + 1 < argc) {
      block_size = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--block_count") == 0 && i + 1 < argc) {
      block_count = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
      file_path = argv[++i];
    } else if (strcmp(argv[i], "--range") == 0 && i + 1 < argc) {
      char* arg = argv[++i];
      char* dash = strchr(arg, '-');
      if (!dash) {
        fprintf(stderr, "--range must be L-R\n");
        return 1;
      }
      *dash = '\0';
      range_start = atoi(arg);
      // range_end не используем — оставлено под совместимость
    } else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
      const char* v = argv[++i];
      if (strcmp(v, "random") == 0)
        random_mode = 1;
      else if (strcmp(v, "sequence") == 0)
        random_mode = 0;
      else {
        fprintf(stderr, "Unknown value for --type\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = atoi(argv[++i]);
      if (iterations <= 0) {
        fprintf(stderr, "iterations must be > 0\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--rw") == 0 && i + 1 < argc) {
      // --rw write_then_read | write | read
      const char* v = argv[++i];
      if (strcmp(v, "write_then_read") == 0) {
        do_write = 1;
        do_read = 1;
      } else if (strcmp(v, "write") == 0) {
        do_write = 1;
        do_read = 0;
      } else if (strcmp(v, "read") == 0) {
        do_write = 0;
        do_read = 1;
      } else {
        fprintf(stderr, "Unknown mode for --rw\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--help") == 0) {
      printf(
          "Usage: io_loader "
          "--cache vtpc|ospc "
          "--rw write_then_read|write|read "
          "--block_size <B> "
          "--block_count <N> "
          "--file <path> "
          "[--range L-R] "
          "[--type sequence|random] "
          "[--iterations K]\n");
      return 0;
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      return 1;
    }
  }

  if (!file_path || block_size <= 0 || block_count <= 0) {
    fprintf(stderr,
            "Error: --file, --block_size, --block_count are required and must "
            "be > 0\n");
    return 1;
  }

  if (strcmp(cache, "vtpc") == 0) {
    use_vtpc = 1;
  } else if (strcmp(cache, "ospc") == 0) {
    use_vtpc = 0;
  } else {
    fprintf(stderr, "--cache must be vtpc|ospc\n");
    return 1;
  }

  // Если делаем write — открываем с O_RDWR|O_CREAT|O_TRUNC, иначе O_RDONLY
  int flags = do_write ? (O_RDWR | O_CREAT | O_TRUNC) : (O_RDONLY);

  int fd = -1;
  if (use_vtpc) {
    fd = vtpc_open(file_path, flags, 0644);
    if (fd < 0)
      die("vtpc_open");
  } else {
    fd = open(file_path, flags, 0644);
    if (fd < 0)
      die("open");
  }

  void* buf = NULL;
  if (posix_memalign(&buf, 4096, (size_t)block_size) != 0)
    die("posix_memalign");
  memset(buf, 0, (size_t)block_size);

  uint64_t step = 0;

  // ---- WRITE PHASE ----
  if (do_write) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int r = 0; r < iterations; r++) {
      for (int i = 0; i < block_count; i++, step++) {
        int block_index = random_mode ? (rand() % block_count) : i;
        off_t pos = (off_t)range_start + (off_t)block_index * (off_t)block_size;

        if (use_vtpc) {
          if (vtpc_lseek(fd, pos, SEEK_SET) < 0)
            die("vtpc_lseek");
          if (!random_mode)
            (void)vtpc_advice(fd, pos, step + 1);

          ssize_t wr = vtpc_write(fd, buf, (size_t)block_size);
          if (wr != block_size)
            die("vtpc_write");
        } else {
          if (lseek(fd, pos, SEEK_SET) < 0)
            die("lseek");

          ssize_t wr = write(fd, buf, (size_t)block_size);
          if (wr != block_size)
            die("write");
        }
      }
    }

    if (use_vtpc) {
      if (vtpc_fsync(fd) != 0)
        die("vtpc_fsync");
    } else {
      if (fsync(fd) != 0)
        die("fsync");
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = diff_nsec(t0, t1);
    printf("WRITE time: %ld.%09lds\n", ns / 1000000000L, ns % 1000000000L);
  }

  // ---- READ PHASE ----
  if (do_read) {
    // вернемся в начало
    if (use_vtpc) {
      if (vtpc_lseek(fd, 0, SEEK_SET) < 0)
        die("vtpc_lseek");
    } else {
      if (lseek(fd, 0, SEEK_SET) < 0)
        die("lseek");
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int r = 0; r < iterations; r++) {
      for (int i = 0; i < block_count; i++, step++) {
        int block_index = random_mode ? (rand() % block_count) : i;
        off_t pos = (off_t)range_start + (off_t)block_index * (off_t)block_size;

        if (use_vtpc) {
          if (vtpc_lseek(fd, pos, SEEK_SET) < 0)
            die("vtpc_lseek");
          if (!random_mode)
            (void)vtpc_advice(fd, pos, step + 1);

          ssize_t rd = vtpc_read(fd, buf, (size_t)block_size);
          if (rd < 0)
            die("vtpc_read");
        } else {
          if (lseek(fd, pos, SEEK_SET) < 0)
            die("lseek");

          ssize_t rd = read(fd, buf, (size_t)block_size);
          if (rd < 0)
            die("read");
        }
      }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = diff_nsec(t0, t1);
    printf("READ time:  %ld.%09lds\n", ns / 1000000000L, ns % 1000000000L);
  }

  // статистика — только VTPC
  if (use_vtpc)
    vtpc_print_stats(fd);

  // close
  if (use_vtpc) {
    if (vtpc_close(fd) != 0)
      die("vtpc_close");
  } else {
    if (close(fd) != 0)
      die("close");
  }

  free(buf);
  return 0;
}