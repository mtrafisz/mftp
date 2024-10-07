#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "socket.h"
#include "utils.h"

bool socket_set_nonblocking(socket_t* sock, bool to) {
    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) {
        log_syserr("Failed to get socket flags");
        return false;
    }

    if (to) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(sock->fd, F_SETFL, flags) < 0) {
        log_syserr("Failed to set socket flags");
        return false;
    }

    return true;
}

bool socket_bind_tcp(socket_t* out_socket, uint32_t haddr, uint16_t hport) {
    out_socket->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (out_socket->fd < 0) {
        log_syserr("Failed to create socket");
        return false;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(haddr),
        .sin_port = htons(hport),
        .sin_zero = { 0 },
    };

    if (bind(out_socket->fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        log_syserr("Failed to bind socket");
        close(out_socket->fd);
        return false;
    }

    if (!socket_set_nonblocking(out_socket, true)) {
        log_err("Failed to set socket non-blocking");
        close(out_socket->fd);
        return false;
    }

    int optval = 1;
    if (setsockopt(out_socket->fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        log_syserr("Failed to set socket options");
        close(out_socket->fd);
        return false;
    }

    struct sockaddr_in bound_addr = { 0 };
    socklen_t bound_addr_len = sizeof(bound_addr);
    if (getsockname(out_socket->fd, (struct sockaddr*)&bound_addr, &bound_addr_len) < 0) {
        log_syserr("Failed to get socket name");
        close(out_socket->fd);
        return false;
    }

    out_socket->haddr = ntohl(bound_addr.sin_addr.s_addr);
    out_socket->hport = ntohs(bound_addr.sin_port);

    return true;
}

void socket_cleanup(socket_t* sock) {
    shutdown(sock->fd, SHUT_RDWR);
    close(sock->fd);
    sock->fd = -1;
}
