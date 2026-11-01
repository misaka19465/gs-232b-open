#ifndef TEST_LWIP_SOCKETS_H
#define TEST_LWIP_SOCKETS_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

ssize_t fake_ws_socket_send(int fd, const void *bytes, size_t size, int flags);
int fake_ws_getsockname(int fd, struct sockaddr *address, socklen_t *length);
int fake_ws_socket_shutdown(int fd, int how);
#define send fake_ws_socket_send
#define getsockname fake_ws_getsockname
#define shutdown fake_ws_socket_shutdown
#endif
