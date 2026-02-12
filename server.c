#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

// send_file
#include <fcntl.h>
// send dir
#include <sys/types.h>
#include <dirent.h>
// get
#include <sys/stat.h>

#define NB_CONNECTION 4
#define OVER_BUF 4

int keep_running = 1;
int listen_fd;

int connections_fd[NB_CONNECTION];
pthread_mutex_t fds_mutex;

void intHandler(int dummy) {
  keep_running = 0;
  close(listen_fd);
  pthread_mutex_lock(&fds_mutex);
  for (int i=0; i<NB_CONNECTION; i++) {
    if (connections_fd[i] >= 0) {
      printf("close %d\n", i);
      close(connections_fd[i]);
    }
  }
  exit(0);
}

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
  /* if (file[1] == 0) { */
  /*   //send(fd, "HTTP/1.0 200 OK\nContent-Type: text/html\nContent-Length: 93\n\n<!DOCTYPE html><html><head><title>TEST</title></head><body><h1>TOTO</h1></body></html>", 146, 0); */
  /*   char *str = "HTTP/1.0 200 OK\nContent-Type: text/html\n\n<!DOCTYPE html><html><head><title>TEST</title></head><body><h1>TOTO</h1></body></html>"; */
  /*   send(fd, str, strlen(str), 0); */
  /* } else { */
  /*   send(fd, "else", 4, 0); */
  /* } */

  free(file);
}

struct connection_args_s {
  int fd;
  int number;
  int index;
};
typedef struct connection_args_s connection_args_t;

void* connection(void *args) {
  connection_args_t *param = (connection_args_t*) args;
  int connection_fd = param->fd;

  //start_read:
  char full_buffer[100 + OVER_BUF];
  memset(full_buffer, 'a', 100+OVER_BUF);
  char *buffer = full_buffer+OVER_BUF;
  char get_str[100];
  int getOff = 0;
  while (keep_running) {
    ssize_t size = read(connection_fd, buffer, 100);
    //printf("\n\"%ld\"\n", size);
    if (size == 0) {
      break;
    }
    char *getpos = strstr(full_buffer, "GET");
    //printf("\n\nFULL:\n\"%s\"\nENDFULL\n\n", full_buffer);
    if (getOff) {
      getpos = buffer;
    }
    if (getpos) {
      //printf("@");
      char *end = strchr(getpos, '\n');
      if (end) {
        ssize_t getsize = end-getpos;
        strncpy(get_str+getOff, getpos, getsize);
        get_str[getOff+getsize] = 0;
        //printf("%s\n", get_str);
        fflush(stdout);
        getOff = 0;
      } else {
        ssize_t getsize = (size+OVER_BUF) - (getpos - full_buffer);
        strncpy(get_str+getOff, getpos, getsize);
        getOff += getsize;
      }
    }

    if (strstr(full_buffer, "\r\n\r\n") || strstr(full_buffer, "\n\n")) {
      //printf("END, -> send\n");
      fflush(stdout);
      char *file = strchr(get_str, ' ');
      if (file) {
        file++;
        *(strchr(file, ' ')) = 0;
        printf("FILE : \"%s\"\n", file);
        get(connection_fd, file);
      }
      //close(connection_fd);
      //goto acc;
      //goto start_read;
      break;
    }

    buffer[size] = 0;
    for (int i=0; i<OVER_BUF; i++) {
      full_buffer[i] = buffer[size-OVER_BUF+i];
    }
    /* for (ssize_t i=0; i<size; i++) { */
    /*   printf("%d, ", buffer[i]); */
    /* } */
  }
  pthread_mutex_lock(&fds_mutex);
  connections_fd[param->index] = -1;
  pthread_mutex_unlock(&fds_mutex);
  printf("END connection %d (fd: %d, index: %d)\n", param->number, connection_fd, param->index);
  close(connection_fd);
  free(args);
  pthread_exit(NULL);
}

int main(void) {
  signal(SIGINT, intHandler);

  //int fd = socket(AF_INET, SOCK_STREAM, PF_INET);
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    fprintf(stderr, "socket failed : %d\n", errno);
    exit(EXIT_FAILURE);
  }

  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    fprintf(stderr, "setsockopt(SO_REUSEADDR) failed\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(6767);
  addr.sin_addr.s_addr = (in_addr_t) 0x0100007f; // 127.0.0.1
  printf("IP address: %s\n",inet_ntoa(addr.sin_addr));
  if (bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
    fprintf(stderr, "bind failed : %d\n", errno);
    close(listen_fd);
    exit(EXIT_FAILURE);
  }
  if (listen(listen_fd, NB_CONNECTION) == -1) {
    fprintf(stderr, "listen failed : %d\n", errno);
    close(listen_fd);
    exit(EXIT_FAILURE);
  }

  memset(connections_fd, -1, NB_CONNECTION * sizeof(int));
  int connection_number = 1;

  while (keep_running) {
    struct sockaddr_in connection_addr;
    socklen_t connection_addr_size;
    connection_addr_size = sizeof(connection_addr);
    int connection_fd = accept(listen_fd, (struct sockaddr *) &connection_addr, &connection_addr_size);
    if (connection_fd  == -1) {
      fprintf(stderr, "accept failed : %d\n", errno);
      close(listen_fd);
      exit(EXIT_FAILURE);
    }
    int index = -1;
    pthread_mutex_lock(&fds_mutex);
    for (int i=0; i<NB_CONNECTION; i++) {
      if (connections_fd[i] == -1) {
        index = i;
        break;
      }
    }
    pthread_mutex_unlock(&fds_mutex);
    if (index < 0) {
      fprintf(stderr, "invalid index (no connection)\n");
      close(connection_fd);
      continue;
    }
    pthread_mutex_lock(&fds_mutex);
    connections_fd[index] = connection_fd;
    pthread_mutex_unlock(&fds_mutex);

    pthread_t thread;
    connection_args_t *args = (connection_args_t*) malloc(sizeof(connection_args_t));
    args->fd = connection_fd;
    args->number = connection_number++;
    args->index = index;
    int err = pthread_create(&thread, NULL, connection, args);
    if (err) {
      pthread_mutex_lock(&fds_mutex);
      connections_fd[index] = -1;
      pthread_mutex_unlock(&fds_mutex);
      fprintf(stderr, "pthread_create failed : %d\n", err);
      close(connection_fd);
      continue;
    }
    printf("START connection %d (fd: %d, index: %d)\n", args->number, args->fd, args->index);
  }

  close(listen_fd);
  return 0;
}
