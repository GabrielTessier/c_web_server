
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

// first elem -> size
int *websocket_fds;

char *str_replace(char *orig, char *rep, char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

void send_404(int fd) {
  char *str = "HTTP/1.0 404 Not Found\nContent-Type: text/html\n\n<!DOCTYPE html><html><head><title>404 not found</title></head><body><h1>404 Not Found</h1></body></html>";
  send(fd, str, strlen(str), 0);
}

void send_file(int fd, char *file_path, struct stat *sb) {
  FILE *file_stream = fopen(file_path, "r");
  if (file_stream) {
    size_t file_path_size = strlen(file_path);
    char *header;
    printf("send file : \"%s\", \"%s\"\n", file_path, file_path + file_path_size - 4);
    if (strncmp(file_path + file_path_size - 4, "html", 4) == 0) {
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
  (void) sb;
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
  if (getcwd(file_path, PATH_MAX) == NULL) {
    strcpy(file_path, "/");
  }
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
  /* printf("Request header : \n"); */
  /* for (int i=0; i<MAX_HEADER; i++) { */
  /*   if (request->headers[i]) { */
  /*     printf("\t%s : %s\n", header_to_string(i), request->headers[i]); */
  /*   } */
  /* } */
  switch (request->method) {
  case M_GET: get(fd, request); break;
  default: break;
  }
}

void websock_callback(int fd, char *content, int opcode) {
  if (opcode == 8) {
    // close
    for (int i = 0; i < websocket_fds[0]; i++) {
      if (websocket_fds[i+1] == fd) {
        websocket_fds[i + 1] = websocket_fds[websocket_fds[0]];
        websocket_fds[0]--;
        websocket_fds =
            (int *)realloc(websocket_fds, sizeof(int) * (websocket_fds[0] + 1));
        break;
      }
    }
    return;
  }
  printf("Content : %s\n", content);
  if (strncmp(content, "toto", 4) == 0) {
    char *rep = "la réponse à toto";
    websocket_send(fd, rep, strlen(rep));
  }
  size_t content_size = strlen(content);
  for (int i=0; i<websocket_fds[0]; i++) {
    websocket_send(websocket_fds[i+1], content, content_size);
  }
}

void websock(int fd, request_t *request) {
  websocket_fds[0]++;
  websocket_fds = (int*) realloc(websocket_fds, sizeof(int) * (websocket_fds[0]+1));
  websocket_fds[websocket_fds[0]] = fd;
  websocket_init(fd, request, websock_callback);
}

void intHandler(int dummy) {
  (void) dummy;
  stop_server(http);
  stop_server(websocket);
}

int main(void) {
  signal(SIGINT, intHandler);
  http = init_server("http", (int[4]){127, 0, 0, 1}, 8001);
  http->request = req;
  //http->log = SERVER_LOG_INFO | SERVER_LOG_WARNING | SERVER_LOG_ERROR;
  websocket = init_server("websocket", (int[4]){127, 0, 0, 1}, 8002); // ws
  websocket->request = websock;
  //websocket->log = SERVER_LOG_INFO | SERVER_LOG_WARNING | SERVER_LOG_ERROR;

  websocket_fds = (int*) malloc(sizeof(int));
  websocket_fds[0] = 0;

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
