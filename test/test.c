#include "response.h"
#include "../server/server.h"
#include "../todo/todo.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

const char *get_file_extension(const char *filepath) {
  const char *dot = strrchr(filepath, '.'); // Find the last occurrence of '.'
  if (!dot || dot == filepath)
    return ""; // No extension found or dot is the first character

  return dot + 1; // Return the extension (skip the dot character)
}

const char *get_mime_type(const char *file_ext) {
  if (strcasecmp(file_ext, "html") == 0 || strcasecmp(file_ext, "htm") == 0) {
    return "text/html";
  } else if (strcasecmp(file_ext, "txt") == 0) {
    return "text/plain";
  } else if (strcasecmp(file_ext, "css") == 0) {
    return "text/css";
  } else if (strcasecmp(file_ext, "js") == 0) {
    return "text/javascript";
  } else if (strcasecmp(file_ext, "jpg") == 0 ||
             strcasecmp(file_ext, "jpeg") == 0) {
    return "image/jpeg";
  } else if (strcasecmp(file_ext, "png") == 0) {
    return "image/png";
  } else if (strcasecmp(file_ext, "gif") == 0) {
    return "image/gif";
  } else if (strcasecmp(file_ext, "webp") == 0) {
    return "image/webp";
  } else {
    return "application/octet-stream";
  }
}

struct Response response_constructor(char *filename, struct Request request,
                                     char *status, int is_json) {
  char *header = (char *)malloc(BUFFER_SIZE * sizeof(char));
  struct Response res;

  size_t response_len = 0;
  if (is_json != 1) {
    char *response = (char *)malloc(BUFFER_SIZE * sizeof(char));
    snprintf(header, BUFFER_SIZE, "%sContent-Type:%s\r\n\r\n", status,
             get_mime_type(get_file_extension(filename)));

    int file_fd = open(filename, O_RDONLY);

    struct stat file_stat;
    fstat(file_fd, &file_stat);

    // copy header to response buffer
    response_len = 0;
    memcpy(response, header, strlen(header));
    response_len += strlen(header);

    // copy file to response buffer
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, response + response_len,
                              BUFFER_SIZE - response_len)) > 0) {
      response_len += bytes_read;
    }
    res.body = response;
    res.size = response_len;
    res.status = status;
    close(file_fd);
  } else {
    char *data = request.body;
    char *method = request.method;
    char *json = (char *)malloc(1000 * sizeof(char));

    if (strcmp(method, "GET") == 0) {
      json = get_all_tasks_in_json();
      status = "HTTP/1.1 200 OK\r\n";
    } else { 
      struct Todo *todo = todo_from_json(data);
      if (strcmp(method, "POST") == 0){
      insert_task(*todo);
      json = "{ \"message\": \"Task created\" }";
      status = "HTTP/1.1 201 Created\r\n";
    } else if (strcmp(method, "DELETE") == 0){
      delete_task(todo->id);
      json = "{ \"message\": \"Task deleted\" }";
      status = "HTTP/1.1 204 No Content\r\n";
    } else if (strcmp(method, "PUT") == 0){
      update_task(todo->id , *todo);
      json = "{ \"message\": \"Task updated\" }";
      status = "HTTP/1.1 202 Accepted\r\n";
    }
    }
      
    char *response = (char *)malloc(BUFFER_SIZE * sizeof(char));
    snprintf(header, BUFFER_SIZE,
             "%sContent-Type: application/json\r\n\r\n%s",
             status, json);
    snprintf(response, BUFFER_SIZE, "%s", header);
    res.body = response;
    res.size = strlen(response);
    res.status = status;
  }
  return res;
  free(header);
}

