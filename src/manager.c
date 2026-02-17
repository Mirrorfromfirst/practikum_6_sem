#define _POSIX_C_SOURCE 200809L
#include "distr.h"
#include "internal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAYLOAD_BUF_SZ 900

typedef struct {
    int fd;
    int alive;
} worker_info_t;

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_timeout = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static void on_sigalrm(int sig) {
    (void)sig;
    g_timeout = 1;
}

static void broadcast(worker_info_t *ws, int n, const char *msg) {
    int i;
    uint8_t type = NET_MSG_ABORT;
    if (strcmp(msg, "SHUTDOWN") == 0) {
        type = NET_MSG_SHUTDOWN;
    }
    for (i = 0; i < n; ++i) {
        if (ws[i].alive != 0) {
            (void)net_send_packet(ws[i].fd, type, NULL, 0U, 5);
        }
    }
}

static void close_all(worker_info_t *ws, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        if (ws[i].fd >= 0) {
            close(ws[i].fd);
            ws[i].fd = -1;
        }
    }
}

int run_manager(const manager_cfg_t *mcfg, const manager_ops_t *ops) {
    int listen_fd = -1;
    worker_info_t *ws = NULL;
    int connected = 0;
    int i;
    struct sigaction sa_new;
    struct sigaction sa_old;

    if (mcfg == NULL || ops == NULL || ops->on_worker_hello == NULL || ops->build_task == NULL ||
        ops->on_worker_result == NULL || mcfg->required_workers < 1 || mcfg->max_time_sec < 1) {
        return 2;
    }

    (void)signal(SIGINT, on_sigint);
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = on_sigalrm;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0;
    if (sigaction(SIGALRM, &sa_new, &sa_old) != 0) {
        return 2;
    }
    g_timeout = 0;
    alarm((unsigned int)mcfg->max_time_sec);
    listen_fd = net_listen(mcfg->host, mcfg->port);
    if (listen_fd < 0) {
        perror("net_listen");
        alarm(0U);
        (void)sigaction(SIGALRM, &sa_old, NULL);
        return 2;
    }

    ws = (worker_info_t *)calloc((size_t)mcfg->required_workers, sizeof(*ws));
    if (ws == NULL) {
        close(listen_fd);
        return 2;
    }
    for (i = 0; i < mcfg->required_workers; ++i) {
        ws[i].fd = -1;
    }

    fprintf(stderr, "[manager] listening on %s:%s, need workers=%d\n",
            mcfg->host, mcfg->port, mcfg->required_workers);

    while (connected < mcfg->required_workers) {
        int fd;
        uint8_t msg_type = 0U;
        uint8_t msg_payload[PAYLOAD_BUF_SZ];
        uint32_t msg_len = 0U;

        if (g_stop != 0) {
            fprintf(stderr, "[manager] interrupted\n");
            goto fail;
        }
        if (g_timeout != 0) {
            fprintf(stderr, "[manager] timeout waiting workers\n");
            goto fail;
        }
        fd = net_accept_timeout(listen_fd, 1);
        if (fd < 0) {
            continue;
        }
        if (net_recv_packet(fd, &msg_type, msg_payload, sizeof(msg_payload), &msg_len, 5) < 0) {
            close(fd);
            continue;
        }
        if (msg_type != NET_MSG_HELLO) {
            close(fd);
            continue;
        }
        if (ops->on_worker_hello(connected, msg_payload, (size_t)msg_len, ops->user_ctx) != 0) {
            close(fd);
            continue;
        }
        ws[connected].fd = fd;
        ws[connected].alive = 1;
        ++connected;
        fprintf(stderr, "[manager] worker#%d joined\n", connected);
    }

    for (i = 0; i < mcfg->required_workers; ++i) {
        uint8_t task_payload[PAYLOAD_BUF_SZ];
        size_t task_len = 0U;
        if (ops->build_task(i, task_payload, sizeof(task_payload), &task_len, ops->user_ctx) != 0) {
            fprintf(stderr, "[manager] build TASK failed\n");
            goto fail_abort;
        }
        if (net_send_packet(ws[i].fd, NET_MSG_TASK, task_payload, (uint32_t)task_len, 5) < 0) {
            fprintf(stderr, "[manager] send TASK failed\n");
            goto fail_abort;
        }
    }

    for (i = 0; i < mcfg->required_workers; ++i) {
        uint8_t msg_type = 0U;
        uint8_t msg_payload[PAYLOAD_BUF_SZ];
        uint32_t msg_len = 0U;

        if (g_stop != 0 || g_timeout != 0) {
            fprintf(stderr, "[manager] timeout or interrupted during collect\n");
            goto fail_abort;
        }
        if (net_recv_packet(ws[i].fd,
                            &msg_type,
                            msg_payload,
                            sizeof(msg_payload),
                            &msg_len,
                            mcfg->max_time_sec) < 0) {
            fprintf(stderr, "[manager] worker#%d disconnected/timeout\n", i);
            goto fail_abort;
        }
        if (msg_type == NET_MSG_RESULT) {
            if (ops->on_worker_result(i, msg_payload, (size_t)msg_len, ops->user_ctx) != 0) {
                fprintf(stderr, "[manager] bad RESULT payload from worker#%d\n", i);
                goto fail_abort;
            }
            continue;
        }
        if (msg_type == NET_MSG_ERROR) {
            fprintf(stderr,
                    "[manager] worker error: %.*s\n",
                    (int)msg_len,
                    (const char *)msg_payload);
        } else {
            fprintf(stderr, "[manager] malformed reply type=%u\n", (unsigned)msg_type);
        }
        goto fail_abort;
    }

    broadcast(ws, mcfg->required_workers, "SHUTDOWN");
    close_all(ws, mcfg->required_workers);
    close(listen_fd);
    alarm(0U);
    (void)sigaction(SIGALRM, &sa_old, NULL);
    free(ws);
    return 0;

fail_abort:
    broadcast(ws, mcfg->required_workers, "ABORT");
fail:
    close_all(ws, mcfg->required_workers);
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    alarm(0U);
    (void)sigaction(SIGALRM, &sa_old, NULL);
    free(ws);
    return 3;
}

