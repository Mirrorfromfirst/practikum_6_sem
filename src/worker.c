#define _POSIX_C_SOURCE 200809L
#include "distr.h"
#include "internal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAYLOAD_BUF_SZ 900

typedef struct {
    int rc;
    uint32_t result_len;
    uint32_t error_len;
    uint8_t result_payload[PAYLOAD_BUF_SZ];
    uint8_t error_payload[PAYLOAD_BUF_SZ];
} task_exec_reply_t;

static volatile sig_atomic_t g_exec_timed_out = 0;

static void on_exec_alarm(int sig) {
    (void)sig;
    g_exec_timed_out = 1;
}

static int run_task_with_timeout(const worker_ops_t *ops,
                                 const uint8_t *payload,
                                 size_t payload_len,
                                 int timeout_sec,
                                 uint8_t *result_payload,
                                 size_t result_payload_sz,
                                 size_t *result_payload_len,
                                 uint8_t *error_payload,
                                 size_t error_payload_sz,
                                 size_t *error_payload_len,
                                 int *timed_out) {
    int pfd[2];
    pid_t pid;
    struct sigaction sa_new;
    struct sigaction sa_old;
    task_exec_reply_t reply;
    ssize_t rd;
    int status;

    if (ops == NULL || payload == NULL || result_payload == NULL || result_payload_len == NULL ||
        error_payload == NULL || error_payload_len == NULL || timed_out == NULL) {
        return -1;
    }
    *timed_out = 0;
    if (pipe(pfd) < 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        int rc;
        size_t out_len = 0U;
        size_t err_len = 0U;
        close(pfd[0]);
        memset(&reply, 0, sizeof(reply));
        rc = ops->execute_task(payload,
                               payload_len,
                               reply.result_payload,
                               sizeof(reply.result_payload),
                               &out_len,
                               reply.error_payload,
                               sizeof(reply.error_payload),
                               &err_len,
                               ops->user_ctx);
        reply.result_len = (uint32_t)out_len;
        reply.error_len = (uint32_t)err_len;
        reply.rc = rc;
        (void)write(pfd[1], &reply, sizeof(reply));
        close(pfd[1]);
        _exit((rc >= 0) ? 0 : 2);
    }

    close(pfd[1]);
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = on_exec_alarm;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0;
    if (sigaction(SIGALRM, &sa_new, &sa_old) != 0) {
        close(pfd[0]);
        (void)kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        return -1;
    }
    g_exec_timed_out = 0;
    alarm((unsigned int)timeout_sec);
    for (;;) {
        pid_t wr = waitpid(pid, &status, 0);
        if (wr == pid) {
            break;
        }
        if (wr < 0 && errno == EINTR) {
            if (g_exec_timed_out != 0) {
                (void)kill(pid, SIGKILL);
                (void)waitpid(pid, NULL, 0);
                alarm(0U);
                (void)sigaction(SIGALRM, &sa_old, NULL);
                close(pfd[0]);
                *timed_out = 1;
                return 1;
            }
            continue;
        }
        alarm(0U);
        (void)sigaction(SIGALRM, &sa_old, NULL);
        close(pfd[0]);
        return -1;
    }
    alarm(0U);
    (void)sigaction(SIGALRM, &sa_old, NULL);

    rd = read(pfd[0], &reply, sizeof(reply));
    close(pfd[0]);
    if (rd != (ssize_t)sizeof(reply)) {
        return -1;
    }
    if ((size_t)reply.result_len > result_payload_sz || (size_t)reply.error_len > error_payload_sz) {
        return -1;
    }
    if (reply.result_len > 0U) {
        memcpy(result_payload, reply.result_payload, reply.result_len);
    }
    if (reply.error_len > 0U) {
        memcpy(error_payload, reply.error_payload, reply.error_len);
    }
    *result_payload_len = reply.result_len;
    *error_payload_len = reply.error_len;
    return reply.rc;
}

int run_worker(const worker_cfg_t *wcfg, const worker_ops_t *ops) {
    int fd = -1;
    uint8_t hello_payload[PAYLOAD_BUF_SZ];
    uint8_t result_payload[PAYLOAD_BUF_SZ];
    uint8_t error_payload[PAYLOAD_BUF_SZ];
    uint8_t in_payload[PAYLOAD_BUF_SZ];
    uint8_t in_type = 0U;
    uint32_t in_len = 0U;
    size_t hello_len = 0U;
    size_t result_len = 0U;
    size_t error_len = 0U;
    int rc;
    int timed_out = 0;

    if (wcfg == NULL || ops == NULL || ops->build_hello == NULL || ops->execute_task == NULL ||
        wcfg->max_cores < 1 || wcfg->max_time_sec < 1) {
        return 2;
    }
    fd = net_connect_timeout(wcfg->host, wcfg->port, 5);
    if (fd < 0) {
        perror("net_connect_timeout");
        return 2;
    }

    rc = ops->build_hello(hello_payload, sizeof(hello_payload), &hello_len, wcfg, ops->user_ctx);
    if (rc != 0) {
        close(fd);
        return 2;
    }
    if (net_send_packet(fd, NET_MSG_HELLO, hello_payload, (uint32_t)hello_len, 5) < 0) {
        close(fd);
        return 2;
    }
    if (net_recv_packet(fd, &in_type, in_payload, sizeof(in_payload), &in_len, wcfg->max_time_sec) < 0) {
        close(fd);
        return 2;
    }
    if (in_type == NET_MSG_ABORT || in_type == NET_MSG_SHUTDOWN) {
        close(fd);
        return 3;
    }
    if (in_type != NET_MSG_TASK) {
        static const uint8_t bad_task[] = "bad_task_format";
        (void)net_send_packet(fd, NET_MSG_ERROR, bad_task, (uint32_t)(sizeof(bad_task) - 1U), 5);
        close(fd);
        return 2;
    }
    result_len = 0U;
    error_len = 0U;
    rc = run_task_with_timeout(ops,
                               in_payload,
                               (size_t)in_len,
                               wcfg->max_time_sec,
                               result_payload,
                               sizeof(result_payload),
                               &result_len,
                               error_payload,
                               sizeof(error_payload),
                               &error_len,
                               &timed_out);
    if (rc < 0) {
        close(fd);
        return 2;
    }
    if (timed_out != 0) {
        static const uint8_t timed_out_msg[] = "timed_out";
        (void)net_send_packet(fd, NET_MSG_ERROR, timed_out_msg, (uint32_t)(sizeof(timed_out_msg) - 1U), 5);
        close(fd);
        return 3;
    }
    if (rc > 0) {
        static const uint8_t task_failed[] = "task_failed";
        if (error_len == 0U) {
            memcpy(error_payload, task_failed, sizeof(task_failed) - 1U);
            error_len = sizeof(task_failed) - 1U;
        }
        (void)net_send_packet(fd, NET_MSG_ERROR, error_payload, (uint32_t)error_len, 5);
        close(fd);
        return 3;
    }
    if (net_send_packet(fd, NET_MSG_RESULT, result_payload, (uint32_t)result_len, 5) < 0) {
        close(fd);
        return 2;
    }

    if (net_recv_packet(fd, &in_type, in_payload, sizeof(in_payload), &in_len, 5) < 0) {
        close(fd);
        return 2;
    }
    if (in_type == NET_MSG_SHUTDOWN) {
        close(fd);
        return 0;
    }
    close(fd);
    return 3;
}

