#ifndef __WEBLIB_H
#define __WEBLIB_H

#include <pthread.h>
#include <stdbool.h>

enum method_e {M_OPTIONS, M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_TRACE, M_CONNECT};

enum header_e {
  HEADER_CACHE_CONTROL = 0, HEADER_CONNECTION, HEADER_DATE, HEADER_PRAGMA, HEADER_TRAILER, HEADER_TRANSFER_ENCODING, HEADER_UPGRADE, HEADER_VIA, HEADER_WARNING, // general header
  HEADER_ACCEPT, HEADER_ACCEPT_CHARSET, HEADER_ACCEPT_ENCODING, HEADER_ACCEPT_LANGUAGE, HEADER_AUTHORIZATION, HEADER_EXPECT, HEADER_FROM, HEADER_HOST, HEADER_IF_MATCH, HEADER_IF_MODIFIED_SINCE, HEADER_IF_NONE_MATCH, HEADER_IF_RANGE, HEADER_IF_UNMODIFIED_SINCE, HEADER_MAX_FORWARDS, HEADER_PROXY_AUTHORIZATION, HEADER_RANGE, HEADER_REFERER, HEADER_TE, HEADER_USER_AGENT,  // request header
  HEADER_ALLOW, HEADER_CONTENT_ENCODING, HEADER_CONTENT_LANGUAGE, HEADER_CONTENT_LENGTH, HEADER_CONTENT_LOCATION, HEADER_CONTENT_MD5, HEADER_CONTENT_RANGE, HEADER_CONTENT_TYPE, HEADER_EXPIRES, HEADER_LAST_MODIFIED, HEADER_EXTENSION_HEADER, // entity header

  MAX_HEADER
};

//enum header_type_e {HT_GENERAL, HT_REQUEST, HT_ENTITY};

//enum general_header_e {GH_CACHE_CONTROL, GH_CONNECTION, GH_DATE, GH_PRAGMA, GH_TRAILER, GH_TRANSFER_ENCODING, GH_UPGRADE, GH_VIA, GH_WARNING};
//enum request_header_e {RH_ACCEPT, RH_ACCEPT_CHARSET, RH_ACCEPT_ENCODING, RH_ACCEPT_LANGUAGE, RH_AUTHORIZATION, RH_EXPECT, RH_FROM, RH_HOST, RH_IF_MATCH, RH_IF_MODIFIED_SINCE, RH_IF_NONE_MATCH, RH_IF_RANGE, RH_IF_UNMODIFIED_SINCE, RH_MAX_FORWARDS, RH_PROXY_AUTHORIZATION, RH_RANGE, RH_REFERER, RH_TE, RH_USER_AGENT};
//enum response_header_e {RH_ACCEPT_RANGES, RH_AGE, RH_ETAG, RH_LOCATION, RH_PROXY_AUTHENTICATE, RH_RETRY_AFTER, RH_SERVER, RH_VARY, RH_WWW_AUTHENTICATE};
//enum entity_header_e {EH_ALLOW, EH_CONTENT_ENCODING, EH_CONTENT_LANGUAGE, EH_CONTENT_LENGTH, EH_CONTENT_LOCATION, EH_CONTENT_MD5, EH_CONTENT_RANGE, EH_CONTENT_TYPE, EH_EXPIRES, EH_LAST_MODIFIED, EH_EXTENSION_HEADER};

struct request_s {
  enum method_e method;
  char *uri;
  char *headers[MAX_HEADER];
};
typedef struct request_s request_t;

struct server_s {
  int ip;
  int port;
  int keep_running;
  int nb_connection;
  int listen_fd;
  int *connections_fd; // size : nb_connection
  pthread_mutex_t fds_mutex;
  bool log;
  void (*request)(int fd, request_t *req);
};
typedef struct server_s server_t;

server_t* init_server(int ipv4[4], int port);
int start_server_async(pthread_t *thread, server_t *serv);
int start_server(server_t *serv);
void stop_server(server_t *serv);
void free_server(server_t *serv);

#endif


#ifdef __WEBLIB_IMPL

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
#include <stdbool.h>

#define BUF_SIZE 1000
#define OVER_BUF 4
#define URL_SIZE 2000

struct connection_args_s {
  server_t *serv;
  int number;
  int index;
};
typedef struct connection_args_s connection_args_t;

void* connection(void *args) {
  connection_args_t *param = (connection_args_t*) args;
  server_t *serv = param->serv;
  int connection_fd = serv->connections_fd[param->index];

  //start_read:
  char full_buffer[BUF_SIZE + OVER_BUF];
  memset(full_buffer, 'a', BUF_SIZE+OVER_BUF);
  char *buffer = full_buffer+OVER_BUF;
  char get_str[URL_SIZE];
  int getOff = 0;
  while (serv->keep_running) {
    ssize_t size = read(connection_fd, buffer, BUF_SIZE);
    //printf("\n\"%ld\"\n", size);
    if (size == 0) {
      break;
    }
    char *getpos = strstr(full_buffer, "GET");
    if (serv->log) printf("\n\nFULL:\n\"%s\"\nENDFULL\n\n", full_buffer);
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
        if (serv->log) printf("FILE : \"%s\"\n", file);
        request_t req = {
          .method = M_GET,
          .uri = file,
          .headers = {0}
        };
        if (serv->request != NULL) {
          serv->request(connection_fd, &req);
        } else {
          fprintf(stderr, "No request function for %s on port %d\n", inet_ntoa((struct in_addr){serv->ip}), serv->port);
        }
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
  pthread_mutex_lock(&(serv->fds_mutex));
  serv->connections_fd[param->index] = -1;
  pthread_mutex_unlock(&(serv->fds_mutex));
  if (serv->log) printf("END connection %d (fd: %d, index: %d)\n", param->number, connection_fd, param->index);
  close(connection_fd);
  free(args);
  pthread_exit(NULL);
}

int ip_to_int(int ipbytes[4]) {
  uint32_t a = ipbytes[0];
  uint32_t b =  ( uint32_t)ipbytes[1] << 8;
  uint32_t c =  ( uint32_t)ipbytes[2] << 16;
  uint32_t d =  ( uint32_t)ipbytes[3] << 24;
  return a+b+c+d;
}

server_t* init_server(int ipv4[4], int port) {
  server_t *serv = (server_t *)malloc(sizeof(server_t));
  serv->ip = ip_to_int(ipv4);
  serv->port = port;
  serv->keep_running = 0;
  serv->nb_connection = 4;
  serv->listen_fd = -1;
  serv->connections_fd = (int*) malloc(sizeof(int) * serv->nb_connection);
  for (int i=0; i<serv->nb_connection; i++) {
    serv->connections_fd[i] = -1;
  }
  pthread_mutex_init(&(serv->fds_mutex), NULL);
  serv->log = false;
  serv->request = NULL;
  return serv;
}

void* start_server_async_wrap(void *serv) {
  int ret = start_server(serv);
  pthread_exit((void*)(intptr_t) ret);
}

int start_server_async(pthread_t *thread, server_t *serv) {
  int err = pthread_create(thread, NULL, start_server_async_wrap, (void*)serv);
  if (err) {
    fprintf(stderr, "pthread_create failed : %d\n", err);
    return 1;
  }
  return 0;
}

int start_server(server_t *serv) {
  serv->keep_running = 1;

  serv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (serv->listen_fd == -1) {
    fprintf(stderr, "socket failed : %d\n", errno);
    return 1;
  }

  if (setsockopt(serv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    fprintf(stderr, "setsockopt(SO_REUSEADDR) failed\n");
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(serv->port);
  addr.sin_addr.s_addr = serv->ip;
  if (serv->log) printf("IP address: %s\nPort : %d\n",inet_ntoa(addr.sin_addr), serv->port);
  if (bind(serv->listen_fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
    fprintf(stderr, "bind failed : %d\n", errno);
    close(serv->listen_fd);
    return 1;
  }
  if (listen(serv->listen_fd, serv->nb_connection) == -1) {
    fprintf(stderr, "listen failed : %d\n", errno);
    close(serv->listen_fd);
    return 1;
  }

  memset(serv->connections_fd, -1, serv->nb_connection * sizeof(int));
  int connection_number = 1;

  while (serv->keep_running) {
    struct sockaddr_in connection_addr;
    socklen_t connection_addr_size;
    connection_addr_size = sizeof(connection_addr);
    int connection_fd = accept(serv->listen_fd, (struct sockaddr *) &connection_addr, &connection_addr_size);
    if (connection_fd  == -1) {
      fprintf(stderr, "accept failed : %d\n", errno);
      close(serv->listen_fd);
      return 1;
    }
    int index = -1;
    pthread_mutex_lock(&(serv->fds_mutex));
    for (int i=0; i<serv->nb_connection; i++) {
      if (serv->connections_fd[i] == -1) {
        index = i;
        break;
      }
    }
    pthread_mutex_unlock(&(serv->fds_mutex));
    if (index < 0) {
      fprintf(stderr, "invalid index (no connection)\n");
      close(connection_fd);
      continue;
    }
    pthread_mutex_lock(&(serv->fds_mutex));
    serv->connections_fd[index] = connection_fd;
    pthread_mutex_unlock(&(serv->fds_mutex));

    pthread_t thread;
    connection_args_t *args = (connection_args_t*) malloc(sizeof(connection_args_t));
    args->serv = serv;
    args->number = connection_number++;
    args->index = index;
    int err = pthread_create(&thread, NULL, connection, args);
    if (err) {
      pthread_mutex_lock(&(serv->fds_mutex));
      serv->connections_fd[index] = -1;
      pthread_mutex_unlock(&(serv->fds_mutex));
      fprintf(stderr, "pthread_create failed : %d\n", err);
      close(connection_fd);
      continue;
    }
    if (serv->log) printf("START connection %d (fd: %d, index: %d)\n", args->number, connection_fd, args->index);
  }

  close(serv->listen_fd);
  return 0;
}

void stop_server(server_t *serv) {
  serv->keep_running = 0;
  close(serv->listen_fd);
  pthread_mutex_lock(&(serv->fds_mutex));
  for (int i=0; i<serv->nb_connection; i++) {
    if (serv->connections_fd[i] >= 0) {
      if (serv->log) printf("close %d\n", i);
      close(serv->connections_fd[i]);
    }
  }
}

void free_server(server_t *serv) {
  free(serv->connections_fd);
  free(serv);
}

#endif
