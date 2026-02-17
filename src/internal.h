#ifndef INTERNAL_H
#define INTERNAL_H

#include <stddef.h>
#include <stdint.h>

enum {
    NET_MSG_HELLO = 1,
    NET_MSG_TASK = 2,
    NET_MSG_RESULT = 3,
    NET_MSG_ERROR = 4,
    NET_MSG_ABORT = 5,
    NET_MSG_SHUTDOWN = 6
};

int net_listen(const char *host, const char *port);
int net_accept_timeout(int listen_fd, int timeout_sec);
int net_connect_timeout(const char *host, const char *port, int timeout_sec);
int net_send_packet(int fd, uint8_t type, const void *payload, uint32_t payload_len, int timeout_sec);
int net_recv_packet(int fd, uint8_t *type, void *payload, size_t payload_cap, uint32_t *payload_len, int timeout_sec);
uint64_t now_ms(void);

#endif

