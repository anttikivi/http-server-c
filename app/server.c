#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int num_places(int n) {
  int r = 1;
  if (n < 0)
    n = (n == INT_MIN) ? INT_MAX : -n;
  while (n > 9) {
    n /= 10;
    r++;
  }
  return r;
}

int main() {
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  printf("Logs from your program will appear here!\n");

  int server_fd;
  struct sockaddr_in client_addr;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    printf("Socket creation failed: %s...\n", strerror(errno));
    return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    printf("SO_REUSEADDR failed: %s \n", strerror(errno));
    return 1;
  }

  struct sockaddr_in serv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(4221),
      .sin_addr = {htonl(INADDR_ANY)},
  };

  if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
    printf("Bind failed: %s \n", strerror(errno));
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    printf("Listen failed: %s \n", strerror(errno));
    return 1;
  }

  printf("Waiting for a client to connect...\n");

  unsigned int client_addr_len = sizeof(client_addr);

  int client_fd =
      accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
  if (client_fd < 0) {
    printf("Response failed: %s \n", strerror(errno));
    return 1;
  }
  printf("Client connected\n");

  char req[1024];
  int bytes_read = read(client_fd, req, sizeof(req));

  printf("%s", req);

  char *method_end = strchr(req, ' ') - 1;
  char *path_start = strchr(req, ' ') + 1;
  char *path_end = strchr(path_start, ' ');

  char method[method_end - req + 1];
  strncpy(method, req, method_end - req + 1);
  // `strncpy` doesn't provide a null terminator.
  method[sizeof(method)] = 0;
  printf("%s\n", method);

  char path[path_end - path_start];
  strncpy(path, path_start, path_end - path_start);
  path[sizeof(path)] = 0;
  printf("%s\n", path);

  if (strcmp(method, "GET") != 0) {
    // TODO: 405 Method Not Allowed
    printf("The server doesn't support %s requests", method);
    return 1;
  }

  int bytes_sent;

  // TODO: Construct the response.
  if (strcmp(path, "/") == 0) {
    char *response = "HTTP/1.1 200 OK\r\n\r\n";

    bytes_sent = write(client_fd, response, strlen(response));
  } else if (strncmp(path, "/echo/", 6) == 0) {
    char param[strlen(path) - 6];
    strncpy(param, path + 6, strlen(path) - 6);
    param[sizeof(param)] = 0;

    char *resf = "HTTP/1.1 200 OK\r\nContent-Type: "
                 "text/plain\r\nContent-Length: %lu\r\n\r\n%s";

    // The lenght of the response is the lenght of the format minus the lenght
    // of the format specifiers plus their lenght.
    // TODO: Maybe just 1024 would also work just fine.
    char response[strlen(resf) - 5 + num_places(strlen(param)) + strlen(param)];
    sprintf(response, resf, strlen(param), param);

    bytes_sent = write(client_fd, response, strlen(response));

    // TODO: Should the endpoint accept a trailing slash?
  } else if (strcmp(path, "/user-agent") == 0) {
    char *headers = strstr(req, "\r\n") + 2;
    char *ua_start = strchr(strcasestr(headers, "user-agent"), ' ') + 1;
    char *ua_end = strstr(ua_start, "\r\n");

    char user_agent[ua_end - ua_start];
    strncpy(user_agent, ua_start, ua_end - ua_start);
    user_agent[sizeof(user_agent)] = 0;

    char *resf = "HTTP/1.1 200 OK\r\nContent-Type: "
                 "text/plain\r\nContent-Length: %lu\r\n\r\n%s";

    // The lenght of the response is the lenght of the format minus the lenght
    // of the format specifiers plus their lenght.
    // TODO: Maybe just 1024 would also work just fine.
    char response[strlen(resf) - 5 + num_places(strlen(user_agent)) +
                  strlen(user_agent)];
    sprintf(response, resf, strlen(user_agent), user_agent);

    bytes_sent = write(client_fd, response, strlen(response));
  } else {
    char *response = "HTTP/1.1 404 Not Found\r\n\r\n";

    bytes_sent = write(client_fd, response, strlen(response));
  }

  close(client_fd);
  close(server_fd);

  return 0;
}
