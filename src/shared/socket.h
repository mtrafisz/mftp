#ifndef _MFTP_SHARED_SOCKET_H_
#define _MFTP_SHARED_SOCKET_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int fd;
    uint32_t haddr;
    uint16_t hport;
} socket_t;

bool socket_set_nonblocking(socket_t* sock, bool to);
bool socket_bind_tcp(socket_t* out_socket, uint32_t haddr, uint16_t hport);
void socket_cleanup(socket_t* sock);

#endif
