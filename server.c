
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define __WEBLIB_IMPL
#include "weblib.h"
#undef __WEBLIB_IMPL

// send_file
#include <fcntl.h>
// send dir
#include <sys/types.h>
#include <dirent.h>
// get
#include <sys/stat.h>

server_t *http;
server_t *websocket;

// Source - https://stackoverflow.com/a/779960
// Posted by jmucchiello, modified by community. See post 'Timeline' for change history
// Retrieved 2026-02-12, License - CC BY-SA 4.0
// You must free the result if result is non-NULL.
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

void send_file(int fd, char *file_path) {
  FILE *file_stream = fopen(file_path, "r");
  if (file_stream) {
    size_t file_path_size = strlen(file_path);
    char *header;
    printf("FF : \"%s\", \"%s\"\n", file_path, file_path + file_path_size - 4);
    if (strncmp(file_path + file_path_size - 4, "html", 4) == 0) {
      printf("HTML\n");
      header = "HTTP/1.0 200 OK\nContent-Type: text/html\n\n";
    } else if (strncmp(file_path + file_path_size - 3, "css", 3) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: text/css\n\n";
    } else if (strncmp(file_path + file_path_size - 2, "js", 2) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: application/javascript\n\n";
    } else if (strncmp(file_path + file_path_size - 4, "jpeg", 4) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: image/jpeg\n\n";
    } else if (strncmp(file_path + file_path_size - 3, "png", 3) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: image/png\n\n";
    } else if (strncmp(file_path + file_path_size - 3, "svg", 3) == 0) {
      header = "HTTP/1.0 200 OK\nContent-Type: image/svg+xml\n\n";
    } else {
      header = "HTTP/1.0 200 OK\nContent-Type: text/plain\n\n";
    }
    send(fd, header, strlen(header), 0);

    char buf[100];
    int size = 0;
    while ((size = fread(buf, 1, 100, file_stream))) {
      buf[size] = 0;
      send(fd, buf, size, 0);
    }
  } else {
    send_404(fd);
  }
}

void send_dir(int fd, char *dir_path, struct stat *sb) {
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
    send(fd, dir_path, dire_path_len, 0);
    if (dir_path[dire_path_len-1] != '/') {
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

void get(int fd, char *file) {
  file = str_replace(file, "%20", " ");
  struct stat sb;
  if (lstat(file, &sb) == -1) {
    send_404(fd);
    free(file);
    return;
  }
  // stat fournit la taille des fichiers (peut-être l'utiliser pour load d'un coup)
  switch (sb.st_mode & S_IFMT) {
  case S_IFDIR:  send_dir(fd, file, &sb);      break;
  case S_IFLNK:  send_file(fd, file);          break;
  case S_IFREG:  send_file(fd, file);          break;
  default:       send_404(fd);                 break;
  }

  free(file);
}

void req(int fd, request_t *request) {
  switch (request->method) {
  case M_GET: get(fd, request->uri); break;
  default: break;
  }
}

void intHandler(int dummy) {
  stop_server(http);
  free_server(http);

  stop_server(websocket);
  free_server(websocket);
}

int main(void) {
  signal(SIGINT, intHandler);
  http = init_server((int[4]){127, 0, 1, 1}, 6767);
  http->request = req;
  websocket = init_server((int[4]){127, 0, 1, 1}, 1234); // ws
  http->log = true;
  pthread_t thread;
  start_server_async(&thread, http);
  start_server(websocket);
}
