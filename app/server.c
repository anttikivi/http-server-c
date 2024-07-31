#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

  printf("Waiting for a client to connect...\n");

  unsigned int client_addr_len = sizeof(client_addr);

  while (1) {
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

    if (client_fd < 0) {
      printf("Accept failed: %s \n", strerror(errno));
      continue;
    }
    printf("Client connected\n");

    printf("Handling a client, the client file descriptor is %d\n", client_fd);

    if (handle_client(client_fd) != 0) {
      printf("Failed to handle the client %d\n", client_fd);
      close(client_fd);
      continue;
    }

    close(client_fd);
  }

  close(server_fd);

  return EXIT_SUCCESS;
}

int handle_client(int client_fd) {
  printf("Started to handle the client %d\n", client_fd);

  char req[1024];
  read(client_fd, req, sizeof(req));

  printf("%s", req);

  char *method = strtok(req, " ");
  char *path = strtok(NULL, " ");

  // For these challenges, we don't need the HTTP version.
  strtok(NULL, "\r\n");

  char *header_token = strtok(NULL, "\r\n");
  char headers[1024][1024];
  unsigned int num_headers = 0;
  while (NULL != header_token) {
    strncpy(headers[num_headers], header_token, strlen(header_token));
    header_token = strtok(NULL, "\r\n");
    num_headers++;
  }

  if (strcmp(method, "GET") != 0) {
    // TODO: 405 Method Not Allowed
    printf("The server doesn't support %s requests", method);
    return EXIT_FAILURE;
  }

  // TODO: Construct the response.
  if (strcmp(path, "/") == 0) {
    // char *response = "HTTP/1.1 200 OK\r\n\r\n";
    char *response = build_response(200, NULL, NULL);
    write(client_fd, response, strlen(response));
    free(response);
  } else if (strncmp(path, "/echo/", 6) == 0) {
    char param[strlen(path) - 6];
    strncpy(param, path + 6, strlen(path) - 6);
    param[sizeof(param)] = '\0';

    char *response = build_response(200, NULL, param);
    write(client_fd, response, strlen(response));

    // TODO: Should the endpoint accept a trailing slash?
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
      char *response = build_response(200, NULL, user_agent);
      write(client_fd, response, strlen(response));
    }
  } else {
    char *response = "HTTP/1.1 404 Not Found\r\n\r\n";

    write(client_fd, response, strlen(response));
  }

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
