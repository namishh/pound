#include "response/response.h"
#include "routing/routing.h"
#include "server/server.h"
#include "todo/todo.h"
#include "string.h"
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
/*
multiline comment bitch
*/
void start(struct Server *server) {
  char buffer[BUFFER_SIZE];
  int new_socket;
  int addrlen = sizeof(server->address);

  add_route("/", "index.html");
  add_route("/about", "about.html");
  add_route("/secret", "static/text.txt");
  add_route("/based", "based.html");
  add_route("/todos", "todos.html");

  add_route("/static/200.jpg", "static/200.jpg");
  add_route("/static/404.jpg", "static/404.jpg");
  add_route("/static/chad.webp", "static/chad.webp");
  add_route("/static/dance.gif", "static/dance.gif");
  add_route("/favicon.ico", "static/favicon.ico");

  add_route("/static/style.css", "static/style.css");
  add_route("/static/app.js", "static/app.js");
  add_route("/static/todo.js", "static/todo.js");

  add_route("/api", " ");
  inorder();
  while (1) {
    printf("Waitng for connections...\n");
    // accept() -> accepts a connection on a socket.
    new_socket = accept(server->sock, (struct sockaddr *)&server->address,
                        (socklen_t *)&addrlen);
    // read() -> read from a file descriptor
    memset(buffer, 0, BUFFER_SIZE * sizeof(char));
    read(new_socket, buffer, BUFFER_SIZE);
    printf("===========BUFFER========\n");
    printf("%s\n", buffer);
    printf("==========================\n");
    struct Request request = request_constructor(buffer);
    printf("Route: %s\n", request.URI);
    printf("Method: %s\n", request.method);
    printf("Body: %s\n", request.body);

    print_headers();
    char *status = "HTTP/1.1 200 OK\r\n";
    char *file;
    int is_json = 0;
    struct Route *route = search(request.URI);
    if (strstr(request.URI, "/api") != 0) {
      file = " ";
      is_json = 1;
    }
    if (route == NULL) {
      status = "HTTP/1.1 404 NOT FOUND\r\n";
      file = "./public/404.html";
      printf("Route not found\n");
    } else {
      char filename[100];
      snprintf(filename, 100, "./public/%s", route->value);
      printf("Route found %s\n", filename);
      file = strdup(filename);
    }
    printf("is_json: %d\n", is_json);
    struct Response response =
        response_constructor(file, request, status, is_json);

    printf("Status: %s\n", response.status);
    printf("Body: %s\n", response.body);

    send(new_socket, response.body, response.size, 0);

    // close the new_socket
    close(new_socket);
  }
}

int main() {
  // AF_INET -> IPv4, SOCK_STREAM -> TCP, 0 -> protocol
  // BACKLOG -> number of connections that can be queued -> 10
  // INADDR_ANY -> any interface
  seed_db();
  printf("%s\n", get_all_tasks_in_json());
  struct Server server =
      server_constructor(AF_INET, SOCK_STREAM, 0, PORT, 10, INADDR_ANY, start);

  server.start(&server);

  return 0;
}
