#define _POSIX_C_SOURCE 200809L
#include "distr.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int run_worker(const worker_cfg_t *wcfg) {
    int fd = -1;
    char line[256];
    int id = 0;
    double a = 0.0;
    double b = 0.0;
    long n = 0;
    int threads = 1;
    int timeout_sec = 0;
    int timed_out = 0;
    double val = 0.0;

    if (wcfg == NULL || wcfg->max_cores < 1 || wcfg->max_time_sec < 1) {
        return 2;
    }
    fd = net_connect_timeout(wcfg->host, wcfg->port, 5);
    if (fd < 0) {
        perror("net_connect_timeout");
        return 2;
    }

    (void)snprintf(line, sizeof(line), "HELLO cores=%d timeout=%d", wcfg->max_cores, wcfg->max_time_sec);
    if (net_send_line(fd, line) < 0) {
        close(fd);
        return 2;
    }
    if (net_recv_line(fd, line, sizeof(line), wcfg->max_time_sec) < 0) {
        close(fd);
        return 2;
    }
    if (strncmp(line, "ABORT", 5) == 0 || strncmp(line, "SHUTDOWN", 8) == 0) {
        close(fd);
        return 3;
    }

    if (sscanf(line, "TASK id=%d a=%lf b=%lf n=%ld threads=%d timeout=%d",
               &id, &a, &b, &n, &threads, &timeout_sec) != 6) {
        (void)net_send_line(fd, "ERROR bad_task_format");
        close(fd);
        return 2;
    }
    if (threads < 1) {
        threads = 1;
    }
    if (threads > wcfg->max_cores) {
        threads = wcfg->max_cores;
    }
    if (timeout_sec < 1) {
        timeout_sec = wcfg->max_time_sec;
    }

    val = integrate_trapz(a, b, n, threads, timeout_sec, &timed_out);
    if (timed_out != 0) {
        (void)net_send_line(fd, "ERROR timed_out");
        close(fd);
        return 3;
    }

    (void)snprintf(line, sizeof(line), "RESULT id=%d value=%.17g", id, val);
    if (net_send_line(fd, line) < 0) {
        close(fd);
        return 2;
    }

    if (net_recv_line(fd, line, sizeof(line), 5) < 0) {
        close(fd);
        return 2;
    }
    if (strncmp(line, "SHUTDOWN", 8) == 0) {
        close(fd);
        return 0;
    }
    close(fd);
    return 3;
}

