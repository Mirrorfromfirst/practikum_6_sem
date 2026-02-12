#define _POSIX_C_SOURCE 200809L
#include "distr.h"
#include "internal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int fd;
    int cores;
    int worker_timeout_sec;
    int alive;
} worker_info_t;

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static void broadcast(worker_info_t *ws, int n, const char *msg) {
    int i;
    for (i = 0; i < n; ++i) {
        if (ws[i].alive != 0) {
            (void)net_send_line(ws[i].fd, msg);
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

int run_manager(const manager_cfg_t *mcfg, const job_cfg_t *job) {
    int listen_fd = -1;
    worker_info_t *ws = NULL;
    uint64_t start_ms;
    uint64_t compute_start_ms;
    int connected = 0;
    int i;
    int total_cores = 0;
    double total = 0.0;

    if (mcfg == NULL || job == NULL || mcfg->required_workers < 1 || mcfg->max_time_sec < 1 || job->n < 1) {
        return 2;
    }

    (void)signal(SIGINT, on_sigint);
    listen_fd = net_listen(mcfg->host, mcfg->port);
    if (listen_fd < 0) {
        perror("net_listen");
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
    start_ms = now_ms();

    while (connected < mcfg->required_workers) {
        int fd;
        char line[256];
        int cores = 1;
        int timeout_sec = 10;

        if (g_stop != 0) {
            fprintf(stderr, "[manager] interrupted\n");
            goto fail;
        }
        if (now_ms() > start_ms + (uint64_t)mcfg->max_time_sec * 1000ULL) {
            fprintf(stderr, "[manager] timeout waiting workers\n");
            goto fail;
        }
        fd = net_accept_timeout(listen_fd, 1);
        if (fd < 0) {
            continue;
        }
        if (net_recv_line(fd, line, sizeof(line), 5) < 0 ||
            sscanf(line, "HELLO cores=%d timeout=%d", &cores, &timeout_sec) != 2) {
            close(fd);
            continue;
        }
        if (cores < 1) {
            cores = 1;
        }
        if (timeout_sec < 1) {
            timeout_sec = 1;
        }
        ws[connected].fd = fd;
        ws[connected].cores = cores;
        ws[connected].worker_timeout_sec = timeout_sec;
        ws[connected].alive = 1;
        total_cores += cores;
        ++connected;
        fprintf(stderr, "[manager] worker#%d joined cores=%d timeout=%d\n",
                connected, cores, timeout_sec);
    }

    compute_start_ms = now_ms();
    {
        double left = job->a;
        int prefix_cores = 0;
        long assigned_n = 0;
        for (i = 0; i < mcfg->required_workers; ++i) {
            char line[256];
            double right;
            long ni;
            prefix_cores += ws[i].cores;
            if (i == mcfg->required_workers - 1) {
                right = job->b;
                ni = job->n - assigned_n;
            } else {
                right = job->a + (job->b - job->a) * ((double)prefix_cores / (double)total_cores);
                ni = (long)((double)job->n * ((double)ws[i].cores / (double)total_cores));
                if (ni < 1) {
                    ni = 1;
                }
                if (assigned_n + ni > job->n) {
                    ni = job->n - assigned_n;
                }
            }
            assigned_n += ni;
            (void)snprintf(line, sizeof(line),
                           "TASK id=%d a=%.17g b=%.17g n=%ld threads=%d timeout=%d",
                           i, left, right, ni, ws[i].cores, ws[i].worker_timeout_sec);
            if (net_send_line(ws[i].fd, line) < 0) {
                fprintf(stderr, "[manager] send TASK failed\n");
                goto fail_abort;
            }
            left = right;
        }
    }

    for (i = 0; i < mcfg->required_workers; ++i) {
        char line[256];
        int id = -1;
        double val = 0.0;

        if (g_stop != 0 || now_ms() > start_ms + (uint64_t)mcfg->max_time_sec * 1000ULL) {
            fprintf(stderr, "[manager] timeout or interrupted during collect\n");
            goto fail_abort;
        }
        if (net_recv_line(ws[i].fd, line, sizeof(line), mcfg->max_time_sec) < 0) {
            fprintf(stderr, "[manager] worker#%d disconnected/timeout\n", i);
            goto fail_abort;
        }
        if (sscanf(line, "RESULT id=%d value=%lf", &id, &val) == 2) {
            total += val;
            continue;
        }
        if (strncmp(line, "ERROR ", 6) == 0) {
            fprintf(stderr, "[manager] worker error: %s\n", line + 6);
        } else {
            fprintf(stderr, "[manager] malformed reply: %s\n", line);
        }
        goto fail_abort;
    }

    printf("INTEGRAL=%.12f\n", total);
    printf("TOTAL_TIME_SEC=%.6f\n", (double)(now_ms() - compute_start_ms) / 1000.0);
    printf("TOTAL_CORES=%d\n", total_cores);
    broadcast(ws, mcfg->required_workers, "SHUTDOWN");
    close_all(ws, mcfg->required_workers);
    close(listen_fd);
    free(ws);
    return 0;

fail_abort:
    broadcast(ws, mcfg->required_workers, "ABORT");
fail:
    close_all(ws, mcfg->required_workers);
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    free(ws);
    return 3;
}

