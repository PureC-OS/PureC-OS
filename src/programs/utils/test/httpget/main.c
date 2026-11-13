#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static int wait_fd(int fd, short events, int timeout_ms)
{
  struct pollfd p = {fd, events, 0};
  int r = poll(&p, 1, timeout_ms);
  if (r <= 0)
    return r;
  return p.revents ? 1 : 0;
}

int main(int argc, char **argv)
{
  const char *host = argc > 1 ? argv[1] : "example.com";
  const char *path = argc > 2 ? argv[2] : "/";
  const char *port = argc > 3 ? argv[3] : "80";
  struct addrinfo hints;
  struct addrinfo *info = 0;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  int code = getaddrinfo(host, port, &hints, &info);
  if (code)
  {
    fprintf(stderr, "httpget: %s: %s\n", host, gai_strerror(code));
    return 1;
  }
  char text[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &((struct sockaddr_in *)info->ai_addr)->sin_addr, text,
            sizeof(text));
  fprintf(stderr, "httpget: %s -> %s:%s\n", host, text, port);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    perror("socket");
    return 1;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int rc = connect(fd, info->ai_addr, info->ai_addrlen);
  if (rc < 0 && errno != EINPROGRESS)
  {
    perror("connect");
    return 1;
  }
  if (rc < 0)
  {
    int ready = wait_fd(fd, POLLOUT, 10000);
    if (ready <= 0)
    {
      fprintf(stderr, "httpget: connect timeout\n");
      return 1;
    }
    int error = 0;
    socklen_t length = sizeof(error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
    if (error)
    {
      fprintf(stderr, "httpget: connect: error %d\n", error);
      return 1;
    }
  }
  fcntl(fd, F_SETFL, flags);
  struct sockaddr_in local;
  socklen_t local_length = sizeof(local);
  if (getsockname(fd, (struct sockaddr *)&local, &local_length) == 0)
  {
    inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text));
    fprintf(stderr, "httpget: connected from %s:%u\n", text, ntohs(local.sin_port));
  }

  char request[512];
  int length = snprintf(request, sizeof(request),
                        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: purec-httpget\r\n"
                        "Connection: close\r\n\r\n",
                        path, host);
  if (send(fd, request, (size_t)length, 0) != length)
  {
    perror("send");
    return 1;
  }
  char buffer[2048];
  size_t total = 0;
  for (;;)
  {
    if (wait_fd(fd, POLLIN, 15000) <= 0)
    {
      fprintf(stderr, "\nhttpget: receive timeout\n");
      break;
    }
    ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
    if (got < 0)
    {
      perror("recv");
      break;
    }
    if (got == 0)
      break;
    fwrite(buffer, 1, (size_t)got, stdout);
    total += (size_t)got;
  }
  fflush(stdout);
  fprintf(stderr, "\nhttpget: %lu bytes received\n", (unsigned long)total);
  shutdown(fd, SHUT_RDWR);
  close(fd);
  freeaddrinfo(info);
  return total ? 0 : 1;
}
