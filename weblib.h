#ifndef __WEBLIB_H
#define __WEBLIB_H

#include <pthread.h>

enum server_log_e {
  SERVER_LOG_INFO = 1<<0,
  SERVER_LOG_WARNING = 1<<1,
  SERVER_LOG_ERROR = 1<<2
};

enum method_e {M_UNDEFINED = 0,
               M_OPTIONS, M_GET, M_HEAD, M_POST, M_PUT, M_DELETE, M_TRACE, M_CONNECT};

// general header
#define GENERAL_HEADER_LIST                     \
  X(CACHE_CONTROL)                              \
  X(CONNECTION)                                 \
  X(DATE)                                       \
  X(PRAGMA)                                     \
  X(TRAILER)                                    \
  X(TRANSFER_ENCODING)                          \
  X(UPGRADE)                                    \
  X(VIA)                                        \
  X(WARNING)

// request header
#define REQUEST_HEADER_LIST                     \
  X(ACCEPT)                                     \
  X(ACCEPT_CHARSET)                             \
  X(ACCEPT_ENCODING)                            \
  X(ACCEPT_LANGUAGE)                            \
  X(AUTHORIZATION)                              \
  X(EXPECT)                                     \
  X(FROM)                                       \
  X(HOST)                                       \
  X(IF_MATCH)                                   \
  X(IF_MODIFIED_SINCE)                          \
  X(IF_NONE_MATCH)                              \
  X(IF_RANGE)                                   \
  X(IF_UNMODIFIED_SINCE)                        \
  X(MAX_FORWARDS)                               \
  X(PROXY_AUTHORIZATION)                        \
  X(RANGE)                                      \
  X(REFERER)                                    \
  X(TE)                                         \
  X(USER_AGENT)

// entity header
#define ENTITY_HEADER_LIST                      \
  X(ALLOW)                                      \
  X(CONTENT_ENCODING)                           \
  X(CONTENT_LANGUAGE)                           \
  X(CONTENT_LENGTH)                             \
  X(CONTENT_LOCATION)                           \
  X(CONTENT_MD5)                                \
  X(CONTENT_RANGE)                              \
  X(CONTENT_TYPE)                               \
  X(EXPIRES)                                    \
  X(LAST_MODIFIED)                              \
  X(EXTENSION_HEADER)


enum header_e {
#define X(name) HEADER_##name,
  GENERAL_HEADER_LIST
  REQUEST_HEADER_LIST
  ENTITY_HEADER_LIST
#undef X
  MAX_HEADER
};

#define MAX_HEADER_SIZE 20

//enum header_type_e {HT_GENERAL, HT_REQUEST, HT_ENTITY};

//enum general_header_e {GH_CACHE_CONTROL, GH_CONNECTION, GH_DATE, GH_PRAGMA, GH_TRAILER, GH_TRANSFER_ENCODING, GH_UPGRADE, GH_VIA, GH_WARNING};
//enum request_header_e {RH_ACCEPT, RH_ACCEPT_CHARSET, RH_ACCEPT_ENCODING, RH_ACCEPT_LANGUAGE, RH_AUTHORIZATION, RH_EXPECT, RH_FROM, RH_HOST, RH_IF_MATCH, RH_IF_MODIFIED_SINCE, RH_IF_NONE_MATCH, RH_IF_RANGE, RH_IF_UNMODIFIED_SINCE, RH_MAX_FORWARDS, RH_PROXY_AUTHORIZATION, RH_RANGE, RH_REFERER, RH_TE, RH_USER_AGENT};
//enum response_header_e {RH_ACCEPT_RANGES, RH_AGE, RH_ETAG, RH_LOCATION, RH_PROXY_AUTHENTICATE, RH_RETRY_AFTER, RH_SERVER, RH_VARY, RH_WWW_AUTHENTICATE};
//enum entity_header_e {EH_ALLOW, EH_CONTENT_ENCODING, EH_CONTENT_LANGUAGE, EH_CONTENT_LENGTH, EH_CONTENT_LOCATION, EH_CONTENT_MD5, EH_CONTENT_RANGE, EH_CONTENT_TYPE, EH_EXPIRES, EH_LAST_MODIFIED, EH_EXTENSION_HEADER};

struct request_s {
  enum method_e method;
  char *uri;
  char *http_version;
  char *headers[MAX_HEADER];
  int nb_extra_headers;
  char *(*extra_headers)[2];
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

enum header_e header_to_int(char *header_name);
char *header_to_string(enum header_e header);

server_t* init_server(char *name, int ipv4[4], int port);
int start_server_async(pthread_t *thread, server_t *serv);
int start_server(server_t *serv);
void stop_server(server_t *serv);
void free_server(server_t *serv);

void websocket_init(int fd, request_t *request, void (*callback)(int fd, char *content, int opcode));

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

// websocket
#include "sha1.h"
#define BASE64(n)                                                              \
  do {                                                                         \
    if (n < 26)                                                                \
      n += 'A';                                                                \
    else if (n < 52)                                                           \
      n = (n - 26) + 'a';                                                      \
    else if (n < 62)                                                           \
      n = (n - 52) + '0';                                                      \
    else if (n == 62)                                                          \
      n = '+';                                                                 \
    else if (n == 63)                                                          \
      n = '/';                                                                 \
    else                                                                       \
      n = '=';                                                                 \
  } while (0)


#define BUF_SIZE 1000
//#define BUF_SIZE 3
//#define OVER_BUF 4
#define OVER_BUF 3
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
    fprintf(stderr, "\033[0;31mServer error %s : ", serv->name);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\033[0m\n");
    va_end(ap);
  }
}

void server_log_warning(server_t *serv, char *fmt, ...) {
  if (serv->log & SERVER_LOG_WARNING) {
    va_list ap;
    va_start(ap, fmt);
    printf("\033[0;33mServer warning %s : ", serv->name);
    vprintf(fmt, ap);
    printf("\033[0m\n");
    va_end(ap);
  }
}

void server_log_info(server_t *serv, char *fmt, ...) {
  if (serv->log & SERVER_LOG_INFO) {
    va_list ap;
    va_start(ap, fmt);
    printf("Server info %s : ", serv->name);
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

enum header_e header_to_int(char *header_name) {
  size_t size = strlen(header_name);
  char *header_name_format = (char*) malloc(sizeof(char) * (size+1));
  strncpy(header_name_format, header_name, size + 1);
  header_name_format[size] = 0;
  for (size_t i=0; i<size; i++) {
    if (header_name_format[i] == '-') {
      header_name_format[i] = '_';
    }
  }
#define X(name) if (strcasecmp(header_name_format, #name) == 0) return HEADER_##name;
  GENERAL_HEADER_LIST
  REQUEST_HEADER_LIST
  ENTITY_HEADER_LIST
#undef X
  return MAX_HEADER; // invalid header
}

char *header_to_string(enum header_e header) {
#define X(name) case HEADER_##name: return #name;
  switch (header) {
    GENERAL_HEADER_LIST
    REQUEST_HEADER_LIST
    ENTITY_HEADER_LIST
  default: return NULL;
  }
}

#define read_data(full_buffer, buffer, fd, read_size)           \
  do {                                                          \
    for (int i=0; i<OVER_BUF; i++) {                            \
      full_buffer[i] = buffer[read_size-OVER_BUF+i];            \
    }                                                           \
    read_size = read(fd, buffer, BUF_SIZE);                     \
    buffer[read_size] = 0;                                      \
  } while (0)

char *read_until_nl(int fd, char (*full_buffer_p)[BUF_SIZE + OVER_BUF + 1], char **buffer_p, size_t *read_size_p, size_t start_at, char **end_buffer) {
  char *full_buffer = *full_buffer_p;
  char *buffer = *buffer_p;
  char *start = buffer + start_at;
  size_t read_size = *read_size_p;

  size_t nb_copy_last;

  char *line = NULL;

  char *new_line = strstr(start, "\r\n");
  if (new_line) {
    size_t length = new_line - start + 1;
    line = (char*) malloc(sizeof(char) * length);
    strncpy(line, start, length-1);
    line[length-1] = 0;
    nb_copy_last = new_line - buffer;
    goto end;
  }

  size_t length = strlen(start) + 1;
  line = (char*) malloc(sizeof(char) * length);
  strncpy(line, start, length-1);
  line[length-1] = 0;

  read_data(full_buffer, buffer, fd, read_size);
  while (!(new_line = strstr(full_buffer, "\r\n"))) {
    size_t newlength = length + read_size;
    line = realloc(line, newlength);
    strncpy(line + length - 1, buffer, read_size);
    line[newlength-1] = 0;
    length = newlength;
    read_data(full_buffer, buffer, fd, read_size);
  }

  nb_copy_last = new_line - buffer;
  size_t newlength = length + nb_copy_last;
  line = (char*) realloc(line, newlength);
  if (nb_copy_last > 0) {
    strncpy(line + length - 1, buffer, nb_copy_last);
  }
  line[newlength-1] = 0;

 end:
  *read_size_p = read_size;
  if (nb_copy_last + 2 < read_size) {
    *end_buffer = buffer + nb_copy_last + 2;
  } else {
    size_t add = (nb_copy_last + 2) - read_size;
    read_data(full_buffer, buffer, fd, read_size);
    *end_buffer = buffer + add;
    //*end_buffer = buffer;
  }

  return line;
}

void* connection(void *args) {
  connection_args_t *param = (connection_args_t*) args;
  server_t *serv = param->serv;
  int connection_fd = serv->connections_fd[param->index];

  char full_buffer[BUF_SIZE + OVER_BUF + 1];
  //for (int i=0; i<OVER_BUF; i++) {
  //  full_buffer[i] = 'a';
  //}
  memset(full_buffer, 'a', BUF_SIZE+OVER_BUF+1);
  char *buffer = full_buffer+OVER_BUF;
  char uri[URL_SIZE];
  //int methodOff = 0;

  request_t req;
  req.uri = uri;

  size_t read_size = 0;

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
  read_data(full_buffer, buffer, connection_fd, read_size);
  enum method_e method = get_method(buffer);
  char *uriStart;
  switch (method) {
  case M_GET: uriStart = buffer + 4; break;
  case M_POST: uriStart = buffer + 5; break;
  default: goto end;
  }

  req.method = method;

  server_log_info(serv, "Method : %s", method_to_string(method));

  if (uriStart > buffer + BUF_SIZE) {
    size_t over = uriStart - (buffer + BUF_SIZE);
    while (over > BUF_SIZE) {
      read_data(full_buffer, buffer, connection_fd, read_size);
      over -= BUF_SIZE;
    }
    read_data(full_buffer, buffer, connection_fd, read_size);
    uriStart = buffer + over;
  }

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
    size_t uriPartSize = read_size - (uriStart - buffer);
    size_t uriSize = alreadyPaste + uriPartSize;
    char *uriEnd = uriStart + uriPartSize;
    if (uriSize > URL_SIZE) {
      server_log_error(serv, "Uri trop longue");
      goto end;
    }
    *uriEnd = 0;
    strcpy(uri + alreadyPaste, uriStart);
    alreadyPaste = uriSize;
    read_data(full_buffer, buffer, connection_fd, read_size);
    uriStart = buffer;
    goto copyuri;
  }

  server_log_info(serv, "URI : \"%s\"", uri);

  //read http version, uriEnd : "SP HTTP-Version CRLF ..."
  uriEnd++;

  char *headerStart = NULL;

  req.http_version = read_until_nl(connection_fd, &full_buffer, &buffer, &read_size, uriEnd - buffer, &headerStart);

  server_log_info(serv, "HTTP version : \"%s\"", req.http_version);

  req.nb_extra_headers = 0;

  while (strncmp(headerStart, "\r\n", 2) != 0) {
    // char headerName[MAX_HEADER_SIZE];
    size_t headerNameSize = MAX_HEADER_SIZE;
    char *headerName = (char*) malloc(headerNameSize+1);
    size_t posInHeaderName = 0;
    char *sep = NULL;
    size_t headerReadSize = strlen(headerStart);
    while (!(sep = strchr(headerStart, ':'))) {
      if (posInHeaderName + headerReadSize >= headerNameSize) {
        headerNameSize = posInHeaderName + headerReadSize;
        headerName = (char*) realloc(headerName, headerNameSize+1);
      }
      strncpy(headerName + posInHeaderName, headerStart, headerReadSize);
      posInHeaderName += headerReadSize;
      read_data(full_buffer, buffer, connection_fd, read_size);
      headerStart = buffer;
      headerReadSize = read_size;
    }
    size_t size = sep - headerStart;
    if (posInHeaderName + size >= headerNameSize) {
      headerNameSize = posInHeaderName + size;
      headerName = (char*) realloc(headerName, headerNameSize+1);
    }
    strncpy(headerName + posInHeaderName, headerStart, size);
    posInHeaderName += size;
    headerName[posInHeaderName] = 0;
    if (sep+2 >= buffer + read_size) {
      size_t add = (sep+2) - (buffer + read_size);
      read_data(full_buffer, buffer, connection_fd, read_size);
      sep = buffer+add-2;
    }
    char *headerValue = read_until_nl(connection_fd, &full_buffer, &buffer, &read_size, sep+2 - buffer, &headerStart);
    server_log_info(serv, "header : \"%s\" : \"%s\"", headerName, headerValue);
    if (read_size - (headerStart - buffer) < 2) {
      size_t end_size = read_size - (headerStart - buffer);
      read_data(full_buffer, buffer, connection_fd, read_size);
      headerStart = buffer - end_size;
    }

    enum header_e header_int = header_to_int(headerName);
    if (header_int == MAX_HEADER) {
      server_log_warning(serv, "Invalid header : \"%s\"", headerName);
      req.nb_extra_headers++;
      req.extra_headers = (char *(*)[2]) realloc(req.extra_headers, req.nb_extra_headers * 2 * sizeof(char*));
      req.extra_headers[req.nb_extra_headers - 1][0] = headerName;
      req.extra_headers[req.nb_extra_headers - 1][1] = headerValue;
    } else {
      free(headerName);
      req.headers[header_int] = headerValue;
    }
  }

  // TODO read header

  int first = 1;
  while (!strstr(full_buffer, "\r\n\r\n") && !strstr(full_buffer, "\n\n")) {
    if (first) {
      first = 0;
      server_log_warning(serv, "unread data :\n");
      server_log_warning(serv, "first line : %s\n", buffer);
    }
    read_data(full_buffer, buffer, connection_fd, read_size);
    server_log_warning(serv, "%s\n", buffer);
  }

  if (serv->request != NULL) {
    serv->request(connection_fd, &req);
  } else {
    server_log_error(serv, "No request function");
  }

  free(req.http_version);

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

server_t* init_server(char *name, int ipv4[4], int port) {
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

void websocket_send(int fd, char *msg, size_t size) {
  if (size < 126) {
    char header[2];
    header[0] = 0b10000001;
    header[1] = 0 << 7 | size;
    send(fd, header, 2, 0);
    send(fd, msg, size, 0);
  } else if (size >= 126 && size <= 65535) {
    char header[4];
    header[0] = 0b10000001;
    header[1] = 0 << 7 | 126;
    header[2] = size >> 8;
    header[3] = size & 0xff;
    send(fd, header, 4, 0);
    send(fd, msg, size, 0);
  } else {
    char header[10];
    header[0] = 0b10000001;
    header[1] = 0 << 7 | 126;
    header[2] = (size >> 56) & 0xff;
    header[3] = (size >> 48) & 0xff;
    header[4] = (size >> 40) & 0xff;
    header[5] = (size >> 32) & 0xff;
    header[6] = (size >> 24) & 0xff;
    header[7] = (size >> 16) & 0xff;
    header[8] = (size >> 8) & 0xff;
    header[9] = size & 0xff;
    send(fd, header, 10, 0);
    send(fd, msg, size, 0);
  }
}

void websocket_send_close(int fd) {
  char header[2];
  header[0] = 0b10001000;
  header[1] = 0;
  send(fd, header, 2, 0);
}

int websocket_decode_data_frame(int fd, void (*callback)(int fd, char *content, int opcode)) {
  struct ws_header_s {
    uint8_t fin; // 1
    uint8_t rsv1; // 1
    uint8_t rsv2; // 1
    uint8_t rsv3; // 1
    uint8_t opcode; // 4
    uint8_t mask; // 1
    uint8_t payload_len; // 7
    union {
      uint16_t ext_payload_len_1;
      uint64_t ext_payload_len_2;
    };
    uint32_t mask_key;
  };
  struct ws_header_s header = {0};
  char byte;
  ssize_t nb_read = recv(fd, &byte, 1, MSG_WAITALL);
  if (nb_read == 0) {
    printf("websocket error (%d) -> closed\n", fd);
    return 1;
  }

  header.fin = (byte >> 7) & 1;
  header.rsv1 = (byte >> 6) & 1;
  header.rsv2 = (byte >> 5) & 1;
  header.rsv3 = (byte >> 4) & 1;
  header.opcode = byte & 0xf;

  if (header.rsv1 || header.rsv2 || header.rsv3) {
    // FAIL
    printf("websocket error (%d) -> closed\n", fd);
    return 2;
  }

  nb_read = recv(fd, &byte, 1, MSG_WAITALL);
  if (nb_read == 0) {
    printf("websocket error (%d) -> closed\n", fd);
    return 1;
  }

  header.mask = (byte >> 7) & 1;
  header.payload_len = byte & 0x7f;

  uint64_t len = header.payload_len;

  if (header.payload_len == 126) {
    header.ext_payload_len_1 = 0;
    uint8_t byte1, byte2;
    nb_read = recv(fd, &byte1, 1, MSG_WAITALL);
    nb_read = recv(fd, &byte2, 1, MSG_WAITALL);
    header.ext_payload_len_1 = ((uint16_t)byte1) << 8 | byte2;
    if (nb_read == 0) {
      printf("websocket error (%d) -> closed\n", fd);
      return 1;
    }
    len = header.ext_payload_len_1;
  } else if (header.payload_len == 127) {
    nb_read = recv(fd, &header.ext_payload_len_2, 8, MSG_WAITALL);
    if (nb_read == 0) {
      printf("websocket error (%d) -> closed\n", fd);
      return 1;
    }
    len = header.ext_payload_len_2;
  }

  if (header.mask) {
    nb_read = recv(fd, &header.mask_key, 4, MSG_WAITALL);
    if (nb_read == 0) {
      printf("websocket error (%d) -> closed\n", fd);
      return 1;
    }
  }

  char *buf = (char *)malloc(len+1);
  unsigned int off = 0;
  while ((nb_read = read(fd, buf+off, len - off)) > 0) {
    for (int i = 0; i < nb_read; i++) {
      int j = (i+off)%4;
      buf[i] = buf[i] ^ ((header.mask_key >> (j*8)) & 0xff);
    }
    off = nb_read;
    if (off == len) {
      goto end;
    }
  }
  if (len != 0) {
    // Il y a des données à lire mais elle ne sont pas disponible
    return 3;
  }
end:
  buf[len] = 0;
  callback(fd, buf, header.opcode);
  if (header.opcode == 8) {
    websocket_send_close(fd);
  }
  free(buf);
  return 0;
}

void websocket_init(int fd, request_t *request, void (*callback)(int fd, char *content, int opcode)) {
  char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  for (int i = 0; i < request->nb_extra_headers; i++) {
    if (strcmp(request->extra_headers[i][0], "Sec-WebSocket-Key") == 0) {
      char *concat = (char *)malloc(strlen(request->extra_headers[i][1]) +
                                    strlen(guid) + 1);
      strcpy(concat, request->extra_headers[i][1]);
      strcat(concat, guid);
      char sha[21];
      SHA1(sha, concat, strlen(concat));
      char output[29];
      int i = 0;
      int o = 0;
      while (i < 20) {
        char n1 = (sha[i] & 0xfc) >> 2;
        char n2 = ((sha[i] & 0x3) << 4) | ((sha[i + 1] & 0xf0) >> 4);
        char n3 = ((sha[i + 1] & 0xf) << 2) | ((i + 2 != 20)?((sha[i + 2] & 0xc0) >> 6):0);
        char n4 = (i + 2 != 20)?(sha[i + 2] & 0x3f):64;
        i += 3;
        BASE64(n1);
        BASE64(n2);
        BASE64(n3);
        BASE64(n4);
        output[o] = n1;
        output[o + 1] = n2;
        output[o + 2] = n3;
        output[o + 3] = n4;
        o += 4;
      }
      free(concat);
      send(fd, "HTTP/1.1 101 Switching Protocols\n", 33, 0);
      send(fd, "Upgrade: websocket\n", 19, 0);
      send(fd, "Connection: Upgrade\n", 20, 0);
      send(fd, "Sec-WebSocket-Accept: ", 22, 0);
      send(fd, output, 28, 0);
      /* send(fd, "\nSec-WebSocket-Protocol: chat\n", 30, 0); */
      send(fd, "\r\n\r\n", 4, 0);
    }
  }
  int end = 0;
  do {
    end = websocket_decode_data_frame(fd, callback);
  } while(!end);
}

#endif // WEBLIB_IMPLEMENTATION
#endif // __WEBLIB_H
