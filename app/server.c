#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define THREAD_POOL_SIZE 10

pthread_mutex_t handler_mutex = PTHREAD_MUTEX_INITIALIZER;

struct node {
  int client_fd;
  struct node *next;
};

struct queue {
  struct node *front;
  struct node *rear;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool shutdown;
};

int handle_client(int client_fd);

// Builds the HTTP response with the given parameters.
//
// NOTE: The caller of this function must free the return value's memory after.
char *build_response(const int status, const char *content_type,
                     const char *body);

int num_places(int n) {
  int r = 1;
  if (n < 0) {
    n = (n == INT_MIN) ? INT_MAX : -n;
  }
  while (n > 9) {
    n /= 10;
    r++;
  }
  return r;
}

int strcicmp(char const *a, char const *b) {
  for (;; a++, b++) {
    int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
    if (d != 0 || !*a)
      return d;
  }
}

struct queue *create_queue(void) {
  struct queue *q = malloc(sizeof(struct queue));
  q->front = q->rear = NULL;
  pthread_mutex_init(&q->mutex, NULL);
  pthread_cond_init(&q->cond, NULL);
  q->shutdown = false;
  return q;
}

void enqueue(struct queue *q, int client_fd) {
  struct node *new_node = malloc(sizeof(struct node));
  new_node->client_fd = client_fd;
  new_node->next = NULL;

  pthread_mutex_lock(&q->mutex);
  if (q->rear == NULL) {
    q->front = q->rear = new_node;
  } else {
    q->rear->next = new_node;
    q->rear = new_node;
  }
  pthread_cond_signal(&q->cond);
  pthread_mutex_unlock(&q->mutex);
}

int dequeue(struct queue *q) {
  pthread_mutex_lock(&q->mutex);
  while (q->front == NULL && !q->shutdown) {
    pthread_cond_wait(&q->cond, &q->mutex);
  }

  if (q->shutdown && q->front == NULL) {
    pthread_mutex_unlock(&q->mutex);
    return -1;
  }

  struct node *temp = q->front;
  int client_fd = temp->client_fd;
  q->front = q->front->next;
  if (q->front == NULL) {
    q->rear = NULL;
  }
  free(temp);
  pthread_mutex_unlock(&q->mutex);
  return client_fd;
}

void shutdown_queue(struct queue *q) {
  pthread_mutex_lock(&q->mutex);
  q->shutdown = true;
  pthread_cond_broadcast(&q->cond);
  pthread_mutex_unlock(&q->mutex);
}

void destroy_queue(struct queue *q) {
  while (q->front != NULL) {
    struct node *temp = q->front;
    q->front = q->front->next;
    free(temp);
  }
  pthread_mutex_destroy(&q->mutex);
  pthread_cond_destroy(&q->cond);
  free(q);
}

void *worker_thread(void *arg) {
  printf("Started a worker thread...\n");

  struct queue *q = (struct queue *)arg;

  while (1) {
    int client_fd = dequeue(q);

    if (client_fd < 0) {
      printf("Dequeuing failed: %s \n", strerror(errno));
      continue;
    }
    printf("Handling a client, the client file descriptor is %d\n", client_fd);

    if (handle_client(client_fd) != 0) {
      printf("Failed to handle the client %d\n", client_fd);
    }

    printf("Closing client connection %d\n", client_fd);
    close(client_fd);

    usleep(10000);
  }
}

int main(void) {
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  printf("Logs from your program will appear here!\n");

  int server_fd;
  struct sockaddr_in client_addr;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    printf("Socket creation failed: %s...\n", strerror(errno));
    return EXIT_FAILURE;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    printf("SO_REUSEADDR failed: %s \n", strerror(errno));
    return EXIT_FAILURE;
  }

  struct sockaddr_in serv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(4221),
      .sin_addr = {htonl(INADDR_ANY)},
  };

  if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
    printf("Bind failed: %s \n", strerror(errno));
    return EXIT_FAILURE;
  }

  int connection_backlog = 200;
  if (listen(server_fd, connection_backlog) != 0) {
    printf("Listen failed: %s \n", strerror(errno));
    return EXIT_FAILURE;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8388608);

  struct queue *client_queue = create_queue();
  pthread_t thread_pool[THREAD_POOL_SIZE];

  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    pthread_create(&thread_pool[i], NULL, worker_thread, client_queue);
  }

  unsigned int client_addr_len = sizeof(client_addr);

  unsigned int enqueue_count = 0;
  while (1) {
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_fd >= 0) {
      printf("Main thread enqueues a new client file descriptor: %d\n",
             client_fd);
      enqueue(client_queue, client_fd);
      enqueue_count++;
    }
    printf("The enqueue count is now %d\n", enqueue_count);
  }

  puts("Shutting down...");
  pthread_attr_destroy(&attr);

  shutdown_queue(client_queue);
  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    pthread_join(thread_pool[i], NULL);
  }
  puts("Threads joined");
  destroy_queue(client_queue);
  close(server_fd);

  return EXIT_SUCCESS;
}

int handle_client(int client_fd) {
  pthread_mutex_lock(&handler_mutex);

  printf("Started to handle the client %d\n", client_fd);

  char req[4096] = {0};
  ssize_t total_bytes_read = 0;
  ssize_t bytes_read;

  while ((bytes_read = read(client_fd, req + total_bytes_read,
                            sizeof(req) - 1 - total_bytes_read)) > 0) {
    total_bytes_read += bytes_read;
    if (strstr(req, "\r\n\r\n") != NULL) {
      break;
    }
  }

  if (bytes_read < 0) {
    perror("Error reading from socket");
    return EXIT_FAILURE;
  }

  req[total_bytes_read] = '\0';
  printf("Received request (%zd bytes):\n%s\n", total_bytes_read, req);

  char *method = strtok(req, " ");
  char *path = strtok(NULL, " ");

  if (method == NULL || path == NULL) {
    printf("Invalid request format\n");
    return EXIT_FAILURE;
  }

  printf("Method: %s, Path: %s\n", method, path);

  strtok(NULL, "\r\n");

  char *header_token = strtok(NULL, "\r\n");
  char headers[8][128];
  unsigned int num_headers = 0;
  while (NULL != header_token) {
    printf("Found a header: %s\n", header_token);
    strncpy(headers[num_headers], header_token, strlen(header_token));
    header_token = strtok(NULL, "\r\n");
    num_headers++;
  }

  printf("The total number of headers is %d\n", num_headers);

  char *response;
  if (strcmp(path, "/") == 0) {
    printf("Called the index path\n");
    response = build_response(200, NULL, NULL);
  } else if (strncmp(path, "/echo/", 6) == 0) {
    printf("Called the echo path\n");
    char *param = path + 6;
    response = build_response(200, NULL, param);
  } else if (strcmp(path, "/user-agent") == 0) {
    char user_agent[1024];

    for (unsigned int i = 0; i < num_headers; i++) {
      char h[1024];
      strncpy(h, headers[i], strlen(headers[i]));

      if (strcicmp(strtok(h, ":"), "user-agent") == 0) {
        char *s = strtok(NULL, "\r\n");
        if (s[0] == ' ') {
          strncpy(user_agent, s + 1, strlen(s) - 1);
          user_agent[strlen(s) - 1] = '\0';
        } else {
          strncpy(user_agent, s, strlen(s));
          user_agent[strlen(s)] = '\0';
        }
        break;
      }
    }

    if (user_agent[0] == '\0') {
      perror("No user agent found!");
      // TODO: Probably should return a correct HTTP response.
      return EXIT_FAILURE;
    } else {
      response = build_response(200, NULL, user_agent);
    }
  } else {
    response = build_response(404, NULL, NULL);
  }

  printf("Sending response:\n%s\n", response);

  size_t response_len = strlen(response);
  size_t total_bytes_written = 0;
  while (total_bytes_written < response_len) {
    size_t bytes_written = write(client_fd, response + total_bytes_written,
                                 response_len - total_bytes_written);
    if (bytes_written <= 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("Error writing to socket");
      free(response);
      return EXIT_FAILURE;
    }
    total_bytes_written += bytes_written;
  }

  printf("Response sent (%zd bytes)\n", total_bytes_written);

  free(response);

  pthread_mutex_unlock(&handler_mutex);

  return EXIT_SUCCESS;
}

char *build_response(const int status, const char *content_type,
                     const char *body) {
  // TODO: Check if the buffer size is enough.
  char buf[1024];
  // The current length of the buffer. While building the string, the length is
  // stored without the null terminator.
  size_t len = 0;

  char version[] = "HTTP/1.1 ";
  strcpy(buf, version);
  len += strlen(version);

  // TODO: Check if the buffer size is enough.
  char http_status[64];
  // TODO: Check if the buffer size is enough.
  char status_msg[64];
  size_t status_len = 0;
  if (status == 200) {
    char msg[] = "OK";
    strcpy(status_msg, msg);
    status_len = strlen(msg);
  } else if (status == 400) {
    char msg[] = "Not Found";
    strcpy(status_msg, msg);
    status_len = strlen(msg);
  } else {
    printf("Unsupported HTTP status given to the string builder: %d\n", status);
    return NULL;
  }

  strcat(status_msg, "\r\n");
  status_len += 2;
  sprintf(http_status, "%d %s", status, status_msg);
  // The HTTP status code always has three places.
  status_len += 4;

  strcat(buf, http_status);
  len += status_len;

  printf("The current buffer is %s\n", buf);

  if (body == NULL) {
    strcat(buf, "\r\n");
    len += 2;
  } else if (content_type == NULL) {
    size_t body_len = strlen(body);
    char content_headers[46 + num_places(body_len) + 1];
    sprintf(content_headers,
            "Content-Type: text/plain\r\nContent-Length: %lu\r\n\r\n",
            body_len);
    strcat(buf, content_headers);
    len += strlen(content_headers);
    strcat(buf, body);
    len += body_len;
  } else {
    printf("Unsupported Content-Type given to the string builder: %s\n",
           content_type);
    return NULL;
  }

  // TODO: Implement the actual content types.

  printf("The current buffer is %s\n", buf);

  char *response = malloc((++len) * sizeof(char));
  strcpy(response, buf);

  printf("The built response is %s\n", response);

  return response;
}
