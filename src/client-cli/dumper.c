#include "shared/cmd.h"
#include "shared/utils.h"
#include "shared/socket.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <file> <port>\n", argv[0]);
        return 1;
    }

    FILE* file = fopen(argv[1], "rb");
    if (!file) {
        log_syserr("Couldn't open file %s", argv[1]);
        return 1;
    }

    socket_t local_socket = { 0 };
    if (!socket_bind_tcp(&local_socket, INADDR_ANY, 0)) {
        fclose(file);
        return 1;
    }
    socket_set_nonblocking(&local_socket, false);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(atoi(argv[2])),
        .sin_addr = { .s_addr = inet_addr("127.0.0.1") },
        .sin_zero = { 0 },
    };

    if (connect(local_socket.fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_syserr("Couldn't connect to server");
        goto cleanup;
    }

    char buffer[1024];
    size_t bytes_read, total_sent = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        ssize_t bytes_sent = send(local_socket.fd, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            log_syserr("Failed to send data");
            goto cleanup;
        }
        total_sent += bytes_sent;
    }

    log_info("Sent %lu bytes", total_sent);

cleanup:
    fclose(file);
    socket_cleanup(&local_socket);
    return 0;
}
