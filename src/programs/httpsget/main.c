#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <wolfssl/ssl.h>

#define CA_FILE "/etc/ssl/cert.pem"

static int io_receive(WOLFSSL *ssl, char *buffer, int size, void *context)
{
  (void)ssl;
  int fd = *(int *)context;
  ssize_t got = recv(fd, buffer, (size_t)size, 0);
  if (got > 0)
    return (int)got;
  if (got == 0)
    return WOLFSSL_CBIO_ERR_CONN_CLOSE;
  if (errno == EAGAIN || errno == EINTR)
    return WOLFSSL_CBIO_ERR_WANT_READ;
  if (errno == ECONNRESET)
    return WOLFSSL_CBIO_ERR_CONN_RST;
  return WOLFSSL_CBIO_ERR_GENERAL;
}

static int io_send(WOLFSSL *ssl, char *buffer, int size, void *context)
{
  (void)ssl;
  int fd = *(int *)context;
  ssize_t sent = send(fd, buffer, (size_t)size, 0);
  if (sent >= 0)
    return (int)sent;
  if (errno == EAGAIN || errno == EINTR)
    return WOLFSSL_CBIO_ERR_WANT_WRITE;
  if (errno == EPIPE || errno == ECONNRESET)
    return WOLFSSL_CBIO_ERR_CONN_RST;
  return WOLFSSL_CBIO_ERR_GENERAL;
}

static unsigned char *read_file(const char *path, long *size)
{
  FILE *file = fopen(path, "rb");
  if (!file)
    return 0;
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (length <= 0)
  {
    fclose(file);
    return 0;
  }
  unsigned char *data = malloc((size_t)length);
  if (!data || fread(data, 1, (size_t)length, file) != (size_t)length)
  {
    free(data);
    fclose(file);
    return 0;
  }
  fclose(file);
  *size = length;
  return data;
}

static void tls_error(const char *what, WOLFSSL *ssl, int result)
{
  char text[WOLFSSL_MAX_ERROR_SZ];
  int code = wolfSSL_get_error(ssl, result);
  wolfSSL_ERR_error_string((unsigned long)code, text);
  fprintf(stderr, "httpsget: %s: %s (%d)\n", what, text, code);
}

int main(int argc, char **argv)
{
  const char *host = argc > 1 ? argv[1] : "example.com";
  const char *path = argc > 2 ? argv[2] : "/";
  const char *port = argc > 3 ? argv[3] : "443";
  int insecure = argc > 4 && strcmp(argv[4], "--insecure") == 0;

  struct addrinfo hints;
  struct addrinfo *info = 0;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  int code = getaddrinfo(host, port, &hints, &info);
  if (code)
  {
    fprintf(stderr, "httpsget: %s: %s\n", host, gai_strerror(code));
    return 1;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0 || connect(fd, info->ai_addr, info->ai_addrlen) < 0)
  {
    perror("httpsget: connect");
    return 1;
  }
  fprintf(stderr, "httpsget: tcp connected\n");

  wolfSSL_Init();
  WOLFSSL_CTX *context = wolfSSL_CTX_new(wolfSSLv23_client_method());
  if (!context)
  {
    fprintf(stderr, "httpsget: cannot create TLS context\n");
    return 1;
  }
  wolfSSL_SetIORecv(context, io_receive);
  wolfSSL_SetIOSend(context, io_send);
  if (insecure)
    wolfSSL_CTX_set_verify(context, WOLFSSL_VERIFY_NONE, 0);
  else
  {
    long size = 0;
    unsigned char *bundle = read_file(CA_FILE, &size);
    if (!bundle)
    {
      fprintf(stderr, "httpsget: cannot read %s\n", CA_FILE);
      return 1;
    }
    int loaded = wolfSSL_CTX_load_verify_buffer(context, bundle, size,
                                                WOLFSSL_FILETYPE_PEM);
    free(bundle);
    if (loaded != WOLFSSL_SUCCESS)
    {
      fprintf(stderr, "httpsget: CA bundle rejected (%d)\n", loaded);
      return 1;
    }
    wolfSSL_CTX_set_verify(context, WOLFSSL_VERIFY_PEER, 0);
  }
  WOLFSSL *ssl = wolfSSL_new(context);
  if (!ssl)
  {
    fprintf(stderr, "httpsget: cannot create TLS session\n");
    return 1;
  }
  wolfSSL_SetIOReadCtx(ssl, &fd);
  wolfSSL_SetIOWriteCtx(ssl, &fd);
  wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, host, (word16)strlen(host));
  if (!insecure)
    wolfSSL_check_domain_name(ssl, host);
  int result = wolfSSL_connect(ssl);
  if (result != WOLFSSL_SUCCESS)
  {
    tls_error("handshake", ssl, result);
    return 1;
  }
  fprintf(stderr, "httpsget: %s, %s\n", wolfSSL_get_version(ssl),
          wolfSSL_get_cipher(ssl));

  char request[512];
  int length = snprintf(request, sizeof(request),
                        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: purec-httpsget\r\n"
                        "Accept: */*\r\nConnection: close\r\n\r\n",
                        path, host);
  if (wolfSSL_write(ssl, request, length) != length)
  {
    tls_error("write", ssl, -1);
    return 1;
  }
  char buffer[2048];
  size_t total = 0;
  for (;;)
  {
    int got = wolfSSL_read(ssl, buffer, sizeof(buffer));
    if (got > 0)
    {
      fwrite(buffer, 1, (size_t)got, stdout);
      total += (size_t)got;
      continue;
    }
    int error = wolfSSL_get_error(ssl, got);
    if (error != WOLFSSL_ERROR_ZERO_RETURN && error != SOCKET_PEER_CLOSED_E &&
        error != WOLFSSL_ERROR_SYSCALL)
      tls_error("read", ssl, got);
    break;
  }
  fflush(stdout);
  fprintf(stderr, "\nhttpsget: %lu bytes received\n", (unsigned long)total);
  wolfSSL_shutdown(ssl);
  wolfSSL_free(ssl);
  wolfSSL_CTX_free(context);
  wolfSSL_Cleanup();
  close(fd);
  freeaddrinfo(info);
  return total ? 0 : 1;
}
