#define _POSIX_C_SOURCE 200809L
#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static int set_common_sockopts(int fd) {
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        return -1;
    }
#ifdef SO_REUSEPORT
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    return 0;
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

static int send_all(int fd, const char *buf, size_t n) {
    size_t off = 0U;
    while (off < n) {
        ssize_t w = send(fd, buf + off, n - off, 0);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int net_listen(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it = NULL;
    int listen_fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        return -1;
    }

    for (it = res; it != NULL; it = it->ai_next) {
        listen_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }
        (void)set_common_sockopts(listen_fd);
        if (bind(listen_fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        close(listen_fd);
        listen_fd = -1;
    }
    freeaddrinfo(res);
    if (listen_fd < 0) {
        return -1;
    }
    if (listen(listen_fd, 64) < 0) {
        close(listen_fd);
        return -1;
    }
    return listen_fd;
}

int net_accept_timeout(int listen_fd, int timeout_sec) {
    fd_set rfds;
    struct timeval tv;
    int rc;

    FD_ZERO(&rfds);
    FD_SET(listen_fd, &rfds);
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    rc = select(listen_fd + 1, &rfds, NULL, NULL, &tv);
    if (rc <= 0) {
        return -1;
    }
    return accept(listen_fd, NULL, NULL);
}

int net_connect_timeout(const char *host, const char *port, int timeout_sec) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it = NULL;
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        return -1;
    }

    for (it = res; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (set_nonblock(fd) < 0) {
            close(fd);
            fd = -1;
            continue;
        }
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        if (errno == EINPROGRESS) {
            fd_set wfds;
            struct timeval tv;
            int sel;
            int err = 0;
            socklen_t len = (socklen_t)sizeof(err);
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            tv.tv_sec = timeout_sec;
            tv.tv_usec = 0;
            sel = select(fd + 1, NULL, &wfds, NULL, &tv);
            if (sel > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                break;
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int net_send_line(int fd, const char *line) {
    size_t n = strlen(line);
    if (send_all(fd, line, n) < 0) {
        return -1;
    }
    if (send_all(fd, "\n", 1U) < 0) {
        return -1;
    }
    return 0;
}

int net_recv_line(int fd, char *buf, size_t bufsz, int timeout_sec) {
    size_t i = 0U;
    while (i + 1U < bufsz) {
        fd_set rfds;
        struct timeval tv;
        int rc;
        char c;
        ssize_t r;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        rc = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) {
            return -1;
        }
        r = recv(fd, &c, 1U, 0);
        if (r <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return 0;
}

uint64_t now_ms(void) {
    struct timeval tv;
    (void)gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

