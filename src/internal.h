#ifndef INTERNAL_H
#define INTERNAL_H

#include <stddef.h>
#include <stdint.h>

int net_listen(const char *host, const char *port);
int net_accept_timeout(int listen_fd, int timeout_sec);
int net_connect_timeout(const char *host, const char *port, int timeout_sec);
int net_send_line(int fd, const char *line);
int net_recv_line(int fd, char *buf, size_t bufsz, int timeout_sec);
uint64_t now_ms(void);

#endif

