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
  printf("Generating %d-node %d-regular graph...\n", num_nodes, k);
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
    int neighbors_added = 0;
    while (neighbors_added < k) {
      int neighbor;
      if ((float)rand() / RAND_MAX < forward_prob) {
        neighbor = i + 1 + (rand() % (num_nodes - i - 1));
      } else {
        neighbor = rand() % i;
      }
      int already_added = 0;
      for (int j = 0; j < neighbors_added; j++) {
        if (nodes[i].neighbors[j] == neighbor) {
          already_added = 1;
          break;
        }
      }
      if (!already_added) {
        nodes[i].neighbors[neighbors_added] = neighbor;
        neighbors_added++;
      }
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
  printf("Graph generated successfully in %s\n", filename);
  return 0;
}

int find_node_by_value(
    const char* filename, int target_value, int* found_node, int max_depth
) {
  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    perror("open");
    return -1;
  }
  struct stat st;
  if (fstat(fd, &st) == -1) {
    perror("fstat");
    close(fd);
    return -1;
  }
  int num_nodes = st.st_size / NODE_SIZE;
  Node* nodes = malloc(st.st_size);
  if (!nodes) {
    perror("malloc");
    close(fd);
    return -1;
  }
  if (read(fd, nodes, st.st_size) != st.st_size) {
    perror("read");
    free(nodes);
    close(fd);
    return -1;
  }
  int* visited = calloc(num_nodes, sizeof(int));
  int* queue = malloc(num_nodes * sizeof(int));
  int* depth = malloc(num_nodes * sizeof(int));
  int queue_start = 0, queue_end = 0;
  queue[queue_end] = 0;
  depth[queue_end] = 0;
  queue_end++;
  visited[0] = 1;
  *found_node = -1;
  while (queue_start < queue_end) {
    int current = queue[queue_start];
    int current_depth = depth[queue_start];
    queue_start++;
    if (nodes[current].value == target_value) {
      *found_node = current;
      break;
    }
    if (max_depth > 0 && current_depth >= max_depth)
      continue;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
      int neighbor = nodes[current].neighbors[i];
      if (neighbor >= 0 && neighbor < num_nodes && !visited[neighbor]) {
        visited[neighbor] = 1;
        queue[queue_end] = neighbor;
        depth[queue_end] = current_depth + 1;
        queue_end++;
      }
    }
  }
  free(visited);
  free(queue);
  free(depth);
  free(nodes);
  close(fd);
  return (*found_node != -1) ? 0 : -1;
}

int modify_node(const char* filename, int node_index, int new_value) {
  int fd = open(filename, O_RDWR);
  if (fd == -1) {
    perror("open");
    return -1;
  }
  if (lseek(fd, node_index * NODE_SIZE, SEEK_SET) == -1) {
    perror("lseek");
    close(fd);
    return -1;
  }
  Node node;
  if (read(fd, &node, NODE_SIZE) != NODE_SIZE) {
    perror("read");
    close(fd);
    return -1;
  }
  printf(
      "Node %d: old value = %d, new value = %d\n",
      node_index,
      node.value,
      new_value
  );
  node.value = new_value;
  if (lseek(fd, node_index * NODE_SIZE, SEEK_SET) == -1) {
    perror("lseek");
    close(fd);
    return -1;
  }
  if (write(fd, &node, NODE_SIZE) != NODE_SIZE) {
    perror("write");
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

void print_usage(const char* program_name) {
  printf("Usage: %s [options]\n", program_name);
  printf("Options:\n");
  printf(
      "  --generate --nodes <N> --edges <K> --file <path> [--forward-prob "
      "<P>]\n"
  );
  printf(
      "  --traverse --file <path> --find <value> --modify <new_value> [--depth "
      "<D>]\n"
  );
  printf("  --help\n");
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
    int num_nodes = 0, k = 4;
    float forward_prob = 0.5;
    char* filename = NULL;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--nodes") == 0 && i + 1 < argc)
        num_nodes = atoi(argv[++i]);
      else if (strcmp(argv[i], "--edges") == 0 && i + 1 < argc)
        k = atoi(argv[++i]);
      else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        filename = argv[++i];
      else if (strcmp(argv[i], "--forward-prob") == 0 && i + 1 < argc)
        forward_prob = atof(argv[++i]);
    }
    if (num_nodes <= 0 || k <= 0 || !filename) {
      fprintf(stderr, "Error: Invalid parameters for graph generation\n");
      return 1;
    }
    return generate_graph(filename, num_nodes, k, forward_prob);
  } else if (strcmp(argv[1], "--traverse") == 0) {
    char* filename = NULL;
    int target_value = 0, new_value = 0;
    int max_depth = -1;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        filename = argv[++i];
      else if (strcmp(argv[i], "--find") == 0 && i + 1 < argc)
        target_value = atoi(argv[++i]);
      else if (strcmp(argv[i], "--modify") == 0 && i + 1 < argc)
        new_value = atoi(argv[++i]);
      else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
        max_depth = atoi(argv[++i]);
    }
    if (!filename) {
      fprintf(stderr, "Error: File not specified\n");
      return 1;
    }
    printf("Searching for node with value %d...\n", target_value);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int found_node;
    if (find_node_by_value(filename, target_value, &found_node, max_depth) ==
        0) {
      printf("Found node at index %d\n", found_node);
      if (modify_node(filename, found_node, new_value) == 0) {
        printf("Node modified successfully\n");
      } else {
        fprintf(stderr, "Failed to modify node\n");
        return 1;
      }
    } else {
      printf("Node with value %d not found\n", target_value);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
      seconds--;
      nanoseconds += 1000000000;
    }
    printf("Traversal time: %ld.%09lds\n", seconds, nanoseconds);
    return 0;
  } else {
    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    print_usage(argv[0]);
    return 1;
  }
}
