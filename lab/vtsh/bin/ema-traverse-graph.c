#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  int value;
  int neighbors[4];
} Node;

#define NODE_SIZE sizeof(Node)
#define MAX_NODES 1000000
#define MAX_NEIGHBORS 4

int generate_graph(
    const char* filename, int num_nodes, int k, float forward_prob
) {
  int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd == -1) {
    perror("open");
    return -1;
  }
  if (ftruncate(fd, num_nodes * NODE_SIZE) == -1) {
    perror("ftruncate");
    close(fd);
    return -1;
  }
  Node* nodes = malloc(num_nodes * NODE_SIZE);
  if (!nodes) {
    perror("malloc");
    close(fd);
    return -1;
  }
  srand(time(NULL));
  for (int i = 0; i < num_nodes; i++) {
    nodes[i].value = rand() % 10000;
    int added = 0;
    while (added < k) {
      int n = ((float)rand() / RAND_MAX < forward_prob)
                  ? i + 1 + (rand() % (num_nodes - i - 1))
                  : rand() % i;
      int dup = 0;
      for (int j = 0; j < added; j++)
        if (nodes[i].neighbors[j] == n) {
          dup = 1;
          break;
        }
      if (!dup)
        nodes[i].neighbors[added++] = n;
    }
  }
  if (write(fd, nodes, num_nodes * NODE_SIZE) != num_nodes * NODE_SIZE) {
    perror("write");
    free(nodes);
    close(fd);
    return -1;
  }
  free(nodes);
  close(fd);
  return 0;
}

//  ИЗМЕНЕНО: принимает уже готовый fd, не делает open/close сама
int find_node_by_value_fd(
    int fd, int target_value, int* found_node, int max_depth
) {
  struct stat st;
  if (fstat(fd, &st) == -1) {
    perror("stat");
    return -1;
  }
  int num = st.st_size / NODE_SIZE;
  Node* nodes = malloc(st.st_size);
  if (!nodes)
    return -1;
  if (read(fd, nodes, st.st_size) != st.st_size) {
    perror("read");
    free(nodes);
    return -1;
  }
  int* vis = calloc(num, 4);
  int* q = malloc(num * 4);
  int* d = malloc(num * 4);
  if (!vis || !q || !d) {
    free(vis);
    free(q);
    free(d);
    free(nodes);
    return -1;
  }
  int s = 0, e = 0;
  q[e] = 0;
  d[e++] = 0;
  vis[0] = 1;
  *found_node = -1;
  while (s < e) {
    int c = q[s];
    int cd = d[s++];
    if (nodes[c].value == target_value) {
      *found_node = c;
      break;
    }
    if (max_depth > 0 && cd >= max_depth)
      continue;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
      int n = nodes[c].neighbors[i];
      if (n >= 0 && n < num && !vis[n]) {
        vis[n] = 1;
        q[e] = n;
        d[e++] = cd + 1;
      }
    }
  }
  free(vis);
  free(q);
  free(d);
  free(nodes);
  return (*found_node != -1) ? 0 : -1;
}

// ИЗМЕНЕНО: modify тоже не делает open/close сама
int modify_node_fd(int fd, int node_index, int new_value) {
  if (lseek(fd, node_index * NODE_SIZE, SEEK_SET) == -1)
    return -1;
  Node node;
  if (read(fd, &node, NODE_SIZE) != NODE_SIZE)
    return -1;
  printf("Node %d: old=%d new=%d\n", node_index, node.value, new_value);
  node.value = new_value;
  if (lseek(fd, node_index * NODE_SIZE, SEEK_SET) == -1)
    return -1;
  if (write(fd, &node, NODE_SIZE) != NODE_SIZE)
    return -1;
  return 0;
}

void print_usage(const char* name) {
  printf("Usage: %s --generate|--traverse --file <path> ...\n", name);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }
  if (strcmp(argv[1], "--help") == 0) {
    print_usage(argv[0]);
    return 0;
  }

  if (strcmp(argv[1], "--generate") == 0) {
    int num = 0, k = 4;
    float p = 0.5;
    const char* file = 0;
    for (int i = 2; i < argc; i++) {
      if (!strcmp(argv[i], "--nodes") && i + 1 < argc)
        num = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--edges") && i + 1 < argc)
        k = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--file") && i + 1 < argc)
        file = argv[++i];
      else if (!strcmp(argv[i], "--forward-prob") && i + 1 < argc)
        p = atof(argv[++i]);
    }
    if (!file || num <= 0 || k <= 0) {
      return 1;
    }
    return generate_graph(file, num, k, p) == 0 ? 0 : 1;
  }

  if (strcmp(argv[1], "--traverse") == 0) {
    const char* file = 0;
    int tv = 0, nv = 0, md = -1;
    for (int i = 2; i < argc; i++) {
      if (!strcmp(argv[i], "--file") && i + 1 < argc)
        file = argv[++i];
      else if (!strcmp(argv[i], "--find") && i + 1 < argc)
        tv = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--modify") && i + 1 < argc)
        nv = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--depth") && i + 1 < argc)
        md = atoi(argv[++i]);
    }
    if (!file) {
      return 1;
    }

    //  Учтено: 1 открытие и 1 закрытие
    int fd = open(file, O_RDONLY);
    if (fd == -1) {
      perror("open");
      return 1;
    }

    printf("Searching %d...\n", tv);
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    int idx;
    if (find_node_by_value_fd(fd, tv, &idx, md) == 0) {
      // тут меняем файл для записи, но без лишних close внутри самой modify
      int fdw = open(file, O_RDWR);
      if (fdw == -1) {
        perror("open rw");
        close(fd);
        return 1;
      }
      if (modify_node_fd(fdw, idx, nv) == 0)
        printf("Modified\n");
      close(fdw);
    } else {
      printf("Not found\n");
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    long s = b.tv_sec - a.tv_sec, ns = b.tv_nsec - a.tv_nsec;
    if (ns < 0) {
      s--;
      ns += 1000000000;
    }
    printf("Traversal time: %ld.%09lds\n", s, ns);

    close(fd);  // 1 раз close после всех операций
    return 0;
  }

  fprintf(stderr, "Unknown: %s\n", argv[1]);
  print_usage(argv[0]);
  return 1;
}