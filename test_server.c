
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define WEBLIB_IMPLEMENTATION
#include "weblib.h"

// send_file
#include <fcntl.h>
// send dir
#include <sys/types.h>
#include <dirent.h>
// get
#include <sys/stat.h>
// req
#include <unistd.h>

server_t *http;
server_t *websocket;

// define in string_lib.c
char *str_replace(char *orig, char *rep, char *with);

void send_404(int fd) {
  char *str = "HTTP/1.0 404 Not Found\nContent-Type: text/html\n\n<!DOCTYPE html><html><head><title>404 not found</title></head><body><h1>404 Not Found</h1></body></html>";
  send(fd, str, strlen(str), 0);
}

void send_file(int fd, char *file_path, struct stat *sb) {
  FILE *file_stream = fopen(file_path, "r");
  if (file_stream) {
    size_t file_path_size = strlen(file_path);
    char *header;
    printf("FF : \"%s\", \"%s\"\n", file_path, file_path + file_path_size - 4);
    if (strncmp(file_path + file_path_size - 4, "html", 4) == 0) {
      printf("HTML\n");
      header = "HTTP/1.0 200 OK\nContent-Type: text/html\n";
    } else if (strncmp(file_path + file_path_size - 3, "css", 3) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: text/css\n";
    } else if (strncmp(file_path + file_path_size - 2, "js", 2) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: application/javascript\n";
    } else if (strncmp(file_path + file_path_size - 4, "jpeg", 4) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: image/jpeg\n";
    } else if (strncmp(file_path + file_path_size - 3, "png", 3) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: image/png\n";
    } else if (strncmp(file_path + file_path_size - 3, "svg", 3) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: image/svg+xml\n";
    } else {
      header = "HTTP/1.0 200 OK\nContent-Type: text/plain\n";
    }
    send(fd, header, strlen(header), 0);
    header = "Content-Length: ";
    send(fd, header, strlen(header), 0);
    char strSize[1000];
    snprintf(strSize, 1000, "%ld", sb->st_size);
    send(fd, strSize, strlen(strSize), 0);
    send(fd, "\r\n\r\n", 4, 0);

    char buf[100];
    int size = 0;
    while ((size = fread(buf, 1, 100, file_stream))) {
      buf[size] = 0;
      if (send(fd, buf, size, 0) < 0) {
        fprintf(stderr, "send failed : %d\n", errno);
      }
    }
  } else {
    send_404(fd);
  }
}

void send_dir(int fd, char *uri, char *dir_path, struct stat *sb) {
  DIR *dirp = opendir(dir_path);
  if (!dirp) {
    send_404(fd);
    return;
  }
  size_t dire_path_len = strlen(dir_path);
  char *str = "HTTP/1.0 200 OK\nContent-Type: text/html\n\n<!DOCTYPE html><html><head><title>";
  send(fd, str, strlen(str), 0);
  send(fd, dir_path, dire_path_len, 0);
  str = "</title></head><body>";
  send(fd, str, strlen(str), 0);
  struct dirent *ent;
  while ((ent = readdir(dirp)) != NULL) {
    size_t ent_size = strlen(ent->d_name);
    str = "<a href=\"";
    send(fd, str, strlen(str), 0);
    size_t uri_size = strlen(uri);
    send(fd, uri, uri_size, 0);
    if (uri[uri_size-1] != '/') {
      send(fd, "/", 1, 0);
    }
    send(fd, ent->d_name, ent_size, 0);
    send(fd, "\">", 2, 0);
    send(fd, ent->d_name, ent_size, 0);
    send(fd, "</a><br>", 8, 0);
  }
  str = "</body></html>";
  send(fd, str, strlen(str), 0);
  closedir(dirp);
}

void get(int fd, request_t *request) {
  char file_path[PATH_MAX+1];
  getcwd(file_path, PATH_MAX);
  strncat(file_path, request->uri, PATH_MAX - strlen(file_path));

  char *file = str_replace(file_path, "%20", " ");
  struct stat sb;
  if (lstat(file, &sb) == -1) {
    send_404(fd);
    free(file);
    return;
  }
  // stat fournit la taille des fichiers (peut-être l'utiliser pour load d'un coup)
  switch (sb.st_mode & S_IFMT) {
  case S_IFDIR:  send_dir(fd, request->uri, file, &sb);      break;
  case S_IFLNK:  send_file(fd, file, &sb);     break;
  case S_IFREG:  send_file(fd, file, &sb);     break;
  default:       send_404(fd);                 break;
  }

  free(file);
}

void req(int fd, request_t *request) {
  printf("Request header : \n");
  for (int i=0; i<MAX_HEADER; i++) {
    if (request->headers[i]) {
      printf("\t%s : %s\n", header_to_string(i), request->headers[i]);
    }
  }
  switch (request->method) {
  case M_GET: get(fd, request); break;
  default: break;
  }
}

void intHandler(int dummy) {
  stop_server(http);
  stop_server(websocket);
}

int main(void) {
  signal(SIGINT, intHandler);
  http = init_server("http", (int[4]){127, 0, 1, 1}, 6767);
  http->request = req;
  http->log = SERVER_LOG_INFO | SERVER_LOG_WARNING | SERVER_LOG_ERROR;
  websocket = init_server("websocket", (int[4]){127, 0, 1, 1}, 1234); // ws
  websocket->log = SERVER_LOG_INFO | SERVER_LOG_WARNING | SERVER_LOG_ERROR;

  pthread_t thread;
  start_server_async(&thread, http);
  start_server(websocket);

  // Here websocket is close

  // wait for http to be close
  pthread_join(thread, NULL);

  // free server struct
  free_server(http);
  free_server(websocket);
}
