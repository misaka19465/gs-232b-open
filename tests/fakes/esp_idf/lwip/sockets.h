#ifndef TEST_LWIP_SOCKETS_H
#define TEST_LWIP_SOCKETS_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define IFNAMSIZ 6
#ifndef SO_BINDTODEVICE
#define SO_BINDTODEVICE 0x100b
#endif
struct ifreq { char ifr_name[IFNAMSIZ]; };

ssize_t fake_ws_socket_send(int fd, const void *bytes, size_t size, int flags);
int fake_ws_getsockname(int fd, struct sockaddr *address, socklen_t *length);
int fake_ws_socket_shutdown(int fd, int how);
int fake_ws_socket_setsockopt(int fd, int level, int option, const void *value, socklen_t length);
#define send fake_ws_socket_send
#define getsockname fake_ws_getsockname
#define shutdown fake_ws_socket_shutdown
#define setsockopt fake_ws_socket_setsockopt
#endif
