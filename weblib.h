#ifndef __WEBLIB_H
#define __WEBLIB_H

#include <pthread.h>

enum server_log_e {
  SERVER_LOG_INFO = 1<<0,
  SERVER_LOG_ERROR = 1<<1
};

enum method_e {M_UNDEFINED = 0,
               M_OPTIONS, M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_TRACE, M_CONNECT};

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
  char *name;
  int ip;
  int port;
  pthread_t *thread; // NULL if not in async
  int keep_running;
  int nb_connection;
  int listen_fd;
  int *connections_fd; // size : nb_connection
  pthread_mutex_t fds_mutex;
  enum server_log_e log;
  void (*request)(int fd, request_t *req);
};
typedef struct server_s server_t;

server_t* init_server(const char *name, int ipv4[4], int port);
int start_server_async(pthread_t *thread, server_t *serv);
int start_server(server_t *serv);
void stop_server(server_t *serv);
void free_server(server_t *serv);


#ifdef WEBLIB_IMPLEMENTATION

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
#include <stdarg.h>

#define BUF_SIZE 1000
#define OVER_BUF 4
#define URL_SIZE 2000

struct connection_args_s {
  server_t *serv;
  int number;
  int index;
};
typedef struct connection_args_s connection_args_t;

enum method_e get_method(char *buffer) {
  // strlen is compute at compile time
#define TEST_METHOD(name)                           \
  if (strncmp(buffer, #name, strlen(#name)) == 0) { \
    return M_##name;                                \
  }
  TEST_METHOD(OPTIONS);
  TEST_METHOD(GET);
  TEST_METHOD(HEAD);
  TEST_METHOD(POST);
  TEST_METHOD(PUT);
  TEST_METHOD(DELETE);
  TEST_METHOD(TRACE);
  TEST_METHOD(CONNECT);
#undef TEST_METHOD
  return M_UNDEFINED;
}

void server_log_error(server_t *serv, char *fmt, ...) {
  if (serv->log & SERVER_LOG_ERROR) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "Server %s : ", serv->name);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
  }
}

void server_log_info(server_t *serv, char *fmt, ...) {
  if (serv->log & SERVER_LOG_INFO) {
    va_list ap;
    va_start(ap, fmt);
    printf("Server %s : ", serv->name);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
  }
}

char *method_to_string(enum method_e method) {
  switch (method) {
  case M_OPTIONS : return "OPTIONS";
  case M_GET     : return "GET";
  case M_HEAD    : return "HEAD";
  case M_POST    : return "POST";
  case M_PUT     : return "PUT";
  case M_DELETE  : return "DELETE";
  case M_TRACE   : return "TRACE";
  case M_CONNECT : return "CONNECT";
  default        : return "UNDEFINED";
  }
}

void* connection(void *args) {
  connection_args_t *param = (connection_args_t*) args;
  server_t *serv = param->serv;
  int connection_fd = serv->connections_fd[param->index];

  char full_buffer[BUF_SIZE + OVER_BUF];
  for (int i=0; i<OVER_BUF; i++) {
    full_buffer[i] = 'a';
  }
  //memset(full_buffer, 'a', BUF_SIZE+OVER_BUF);
  char *buffer = full_buffer+OVER_BUF;
  char uri[URL_SIZE];
  int methodOff = 0;

  /* https://datatracker.ietf.org/doc/html/rfc2616#section-5
   * Request = Request-Line              ; Section 5.1
   *           *(( general-header        ; Section 4.5
   *            | request-header         ; Section 5.3
   *            | entity-header ) CRLF)  ; Section 7.1
   *           CRLF
   *           [ message-body ]          ; Section 4.3
   *
   * Request-Line = Method SP Request-URI SP HTTP-Version CRLF
   */
  ssize_t read_size = read(connection_fd, buffer, BUF_SIZE);
  buffer[read_size] = 0;
  enum method_e method = get_method(buffer);
  char *uriStart;
  switch (method) {
  case M_GET: uriStart = buffer + 4; break;
  case M_POST: uriStart = buffer + 5; break;
  default: goto end;
  }

  server_log_info(serv, "Method : %s", method_to_string(method));

  size_t alreadyPaste = 0;
 copyuri:
  char *uriEnd = strchr(uriStart, ' ');
  if (uriEnd) {
    size_t uriPartSize = uriEnd - uriStart;
    size_t uriSize = alreadyPaste + uriPartSize;
    if (uriSize > URL_SIZE) {
      server_log_error(serv, "Uri trop longue");
      goto end;
    }
    *uriEnd = 0;
    strcpy(uri + alreadyPaste, uriStart);
    *uriEnd = ' ';
    alreadyPaste = uriSize;
  } else {
    size_t uriPartSize = BUF_SIZE - (uriStart - buffer);
    size_t uriSize = alreadyPaste + uriPartSize;
    char *uriEnd = uriStart + uriPartSize;
    if (uriSize > URL_SIZE) {
      server_log_error(serv, "Uri trop longue");
      goto end;
    }
    *uriEnd = 0;
    strcpy(uri + alreadyPaste, uriStart);
    alreadyPaste = uriSize;
    read_size = read(connection_fd, buffer, BUF_SIZE);
    uriStart = buffer;
    goto copyuri;
  }

  server_log_info(serv, "URI : \"%s\"", uri);

  // TODO read header
  while (!strstr(full_buffer, "\r\n\r\n") && !strstr(full_buffer, "\n\n")) {
    for (int i=0; i<OVER_BUF; i++) {
      full_buffer[i] = buffer[read_size-OVER_BUF+i];
    }
    read_size = read(connection_fd, buffer, BUF_SIZE);
  }

  request_t req = {
    .method = M_GET,
    .uri = uri,
    .headers = {0}    // TODO
  };
  if (serv->request != NULL) {
    serv->request(connection_fd, &req);
  } else {
    server_log_error(serv, "No request function");
  }

 end:
  pthread_mutex_lock(&(serv->fds_mutex));
  serv->connections_fd[param->index] = -1;
  pthread_mutex_unlock(&(serv->fds_mutex));
  server_log_info(serv, "END connection %d (fd: %d, index: %d)", param->number, connection_fd, param->index);
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

server_t* init_server(const char *name, int ipv4[4], int port) {
  server_t *serv = (server_t *)malloc(sizeof(server_t));
  serv->name = name;
  serv->ip = ip_to_int(ipv4);
  serv->port = port;
  serv->thread = NULL;
  serv->keep_running = 0;
  serv->nb_connection = 4;
  serv->listen_fd = -1;
  serv->connections_fd = (int*) malloc(sizeof(int) * serv->nb_connection);
  for (int i=0; i<serv->nb_connection; i++) {
    serv->connections_fd[i] = -1;
  }
  pthread_mutex_init(&(serv->fds_mutex), NULL);
  serv->log = 0;
  serv->request = NULL;
  return serv;
}

void* start_server_async_wrap(void *serv) {
  int ret = start_server(serv);
  pthread_exit((void*)(intptr_t) ret);
}

int start_server_async(pthread_t *thread, server_t *serv) {
  serv->thread = thread;
  int err = pthread_create(thread, NULL, start_server_async_wrap, (void*)serv);
  if (err) {
    server_log_error(serv, "pthread_create failed : %d", err);
    return 1;
  }
  return 0;
}

int start_server(server_t *serv) {
  serv->keep_running = 1;

  serv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (serv->listen_fd == -1) {
    server_log_error(serv, "socket failed : %d", errno);
    return 1;
  }

  if (setsockopt(serv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
    server_log_info(serv, "setsockopt(SO_REUSEADDR) failed");
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(serv->port);
  addr.sin_addr.s_addr = serv->ip;
  server_log_info(serv, "Start %s:%d", inet_ntoa(addr.sin_addr), serv->port);
  if (bind(serv->listen_fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
    server_log_error(serv, "bind failed : %d", errno);
    close(serv->listen_fd);
    return 1;
  }
  if (listen(serv->listen_fd, serv->nb_connection) == -1) {
    server_log_error(serv, "listen failed : %d", errno);
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
    if (!serv->keep_running) {
      break;
    }
    if (connection_fd == -1) {
      server_log_error(serv, "accept failed : %d", errno);
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
      server_log_error(serv, "invalid index (no connection)");
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
      server_log_error(serv, "pthread_create failed : %d", err);
      close(connection_fd);
      continue;
    }
    server_log_info(serv, "START connection %d (fd: %d, index: %d)", args->number, connection_fd, args->index);
  }

  close(serv->listen_fd);
  server_log_info(serv, "CLOSE");
  return 0;
}

void stop_server(server_t *serv) {
  if (serv->thread) {
    server_log_info(serv, "pthread_cancel");
    pthread_cancel(*serv->thread);
  }
  serv->keep_running = 0;
  close(serv->listen_fd);
  serv->listen_fd = -1;
  pthread_mutex_lock(&(serv->fds_mutex));
  for (int i=0; i<serv->nb_connection; i++) {
    if (serv->connections_fd[i] >= 0) {
      server_log_info(serv, "close %d", i);
      close(serv->connections_fd[i]);
    }
  }
}

void free_server(server_t *serv) {
  free(serv->connections_fd);
  free(serv);
}

#endif // WEBLIB_IMPLEMENTATION
#endif // __WEBLIB_H
