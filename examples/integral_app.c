#define _POSIX_C_SOURCE 200809L
#include "integral_app.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef struct {
    double a;
    double h;
    long i_begin;
    long i_end;
    double *out_partial;
} thr_ctx_t;

typedef struct {
    uint32_t cores_be;
} hello_msg_t;

typedef struct {
    uint32_t id_be;
    uint64_t a_be;
    uint64_t b_be;
    uint64_t n_be;
    uint32_t threads_be;
} task_msg_t;

typedef struct {
    uint32_t id_be;
    uint64_t value_be;
} result_msg_t;

static double f(double x) {
    return 4.0 / (1.0 + x * x);
}

uint64_t integral_now_ms(void) {
    struct timeval tv;
    (void)gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

static uint64_t host_to_be64(uint64_t v) {
    uint64_t hi = (uint64_t)htonl((uint32_t)(v >> 32));
    uint64_t lo = (uint64_t)htonl((uint32_t)(v & 0xffffffffU));
    return (lo << 32) | hi;
}

static uint64_t be64_to_host(uint64_t v) {
    uint64_t hi = (uint64_t)ntohl((uint32_t)(v >> 32));
    uint64_t lo = (uint64_t)ntohl((uint32_t)(v & 0xffffffffU));
    return (lo << 32) | hi;
}

static uint64_t double_to_be64(double d) {
    uint64_t u = 0U;
    memcpy(&u, &d, sizeof(u));
    return host_to_be64(u);
}

static double be64_to_double(uint64_t be) {
    uint64_t u = be64_to_host(be);
    double d = 0.0;
    memcpy(&d, &u, sizeof(d));
    return d;
}

static void *thr_run(void *arg) {
    thr_ctx_t *ctx = (thr_ctx_t *)arg;
    double sum = 0.0;
    long i;

    for (i = ctx->i_begin; i < ctx->i_end; ++i) {
        {
            double x1 = ctx->a + (double)i * ctx->h;
            double x2 = x1 + ctx->h;
            sum += 0.5 * (f(x1) + f(x2)) * ctx->h;
        }
    }
    *ctx->out_partial = sum;
    return NULL;
}

double integrate_trapz(double a, double b, long n, int threads) {
    pthread_t *ths = NULL;
    thr_ctx_t *ctxs = NULL;
    double *parts = NULL;
    double res = 0.0;
    double h;
    long base;
    long rem;
    int t;

    if (n <= 0L || b <= a) {
        return 0.0;
    }
    if (threads < 1) {
        threads = 1;
    }
    if ((long)threads > n) {
        threads = (int)n;
    }

    h = (b - a) / (double)n;

    ths = (pthread_t *)calloc((size_t)threads, sizeof(*ths));
    ctxs = (thr_ctx_t *)calloc((size_t)threads, sizeof(*ctxs));
    parts = (double *)calloc((size_t)threads, sizeof(*parts));
    if (ths == NULL || ctxs == NULL || parts == NULL) {
        free(ths);
        free(ctxs);
        free(parts);
        return 0.0;
    }

    base = n / threads;
    rem = n % threads;
    {
        long cursor = 0L;
        for (t = 0; t < threads; ++t) {
            long span = base + ((t < rem) ? 1L : 0L);
            ctxs[t].a = a;
            ctxs[t].h = h;
            ctxs[t].i_begin = cursor;
            ctxs[t].i_end = cursor + span;
            ctxs[t].out_partial = &parts[t];
            cursor += span;
            (void)pthread_create(&ths[t], NULL, thr_run, &ctxs[t]);
        }
    }
    for (t = 0; t < threads; ++t) {
        (void)pthread_join(ths[t], NULL);
        res += parts[t];
    }

    free(ths);
    free(ctxs);
    free(parts);
    return res;
}

int integral_manager_ctx_init(integral_manager_ctx_t *ctx, int required_workers, integral_job_t job) {
    if (ctx == NULL || required_workers < 1 || job.n < 1 || job.b <= job.a) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->required_workers = required_workers;
    ctx->job = job;
    ctx->next_left = job.a;
    ctx->worker_cores = (int *)calloc((size_t)required_workers, sizeof(*ctx->worker_cores));
    if (ctx->worker_cores == NULL) {
        integral_manager_ctx_free(ctx);
        return -1;
    }
    return 0;
}

void integral_manager_ctx_free(integral_manager_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    free(ctx->worker_cores);
    ctx->worker_cores = NULL;
}

static int cb_on_worker_hello(int worker_index, const uint8_t *hello_payload, size_t hello_payload_len, void *user_ctx) {
    integral_manager_ctx_t *ctx = (integral_manager_ctx_t *)user_ctx;
    hello_msg_t hello;
    int cores;
    if (ctx == NULL || worker_index < 0 || worker_index >= ctx->required_workers) {
        return -1;
    }
    if (hello_payload == NULL || hello_payload_len != sizeof(hello)) {
        return -1;
    }
    memcpy(&hello, hello_payload, sizeof(hello));
    cores = (int)ntohl(hello.cores_be);
    if (cores < 1) {
        cores = 1;
    }
    ctx->worker_cores[worker_index] = cores;
    ctx->total_cores += cores;
    return 0;
}

static int cb_build_task(int worker_index,
                         uint8_t *task_payload,
                         size_t task_payload_sz,
                         size_t *task_payload_len,
                         void *user_ctx) {
    integral_manager_ctx_t *ctx = (integral_manager_ctx_t *)user_ctx;
    task_msg_t msg;
    double right;
    long ni;
    if (ctx == NULL || task_payload == NULL || task_payload_len == NULL || task_payload_sz < sizeof(msg) ||
        worker_index < 0 || worker_index >= ctx->required_workers || ctx->total_cores < 1) {
        return -1;
    }

    ctx->prefix_cores += ctx->worker_cores[worker_index];
    if (worker_index == ctx->required_workers - 1) {
        right = ctx->job.b;
        ni = ctx->job.n - ctx->assigned_n;
    } else {
        right = ctx->job.a +
                (ctx->job.b - ctx->job.a) * ((double)ctx->prefix_cores / (double)ctx->total_cores);
        ni = (long)((double)ctx->job.n * ((double)ctx->worker_cores[worker_index] / (double)ctx->total_cores));
        if (ni < 1) {
            ni = 1;
        }
        if (ctx->assigned_n + ni > ctx->job.n) {
            ni = ctx->job.n - ctx->assigned_n;
        }
    }
    ctx->assigned_n += ni;
    msg.id_be = htonl((uint32_t)worker_index);
    msg.a_be = double_to_be64(ctx->next_left);
    msg.b_be = double_to_be64(right);
    msg.n_be = host_to_be64((uint64_t)(int64_t)ni);
    msg.threads_be = htonl((uint32_t)ctx->worker_cores[worker_index]);
    memcpy(task_payload, &msg, sizeof(msg));
    *task_payload_len = sizeof(msg);
    ctx->next_left = right;
    return 0;
}

static int cb_on_worker_result(int worker_index,
                               const uint8_t *result_payload,
                               size_t result_payload_len,
                               void *user_ctx) {
    integral_manager_ctx_t *ctx = (integral_manager_ctx_t *)user_ctx;
    result_msg_t msg;
    int id;
    double val;
    (void)worker_index;
    if (ctx == NULL) {
        return -1;
    }
    if (result_payload == NULL || result_payload_len != sizeof(msg)) {
        return -1;
    }
    memcpy(&msg, result_payload, sizeof(msg));
    id = (int)ntohl(msg.id_be);
    val = be64_to_double(msg.value_be);
    if (id < 0 || id >= ctx->required_workers) {
        return -1;
    }
    ctx->total += val;
    return 0;
}

manager_ops_t integral_manager_ops(integral_manager_ctx_t *ctx) {
    manager_ops_t ops;
    ops.on_worker_hello = cb_on_worker_hello;
    ops.build_task = cb_build_task;
    ops.on_worker_result = cb_on_worker_result;
    ops.user_ctx = ctx;
    return ops;
}

static int cb_build_hello(uint8_t *out,
                          size_t out_sz,
                          size_t *out_len,
                          const worker_cfg_t *wcfg,
                          void *user_ctx) {
    hello_msg_t msg;
    (void)user_ctx;
    if (out == NULL || out_len == NULL || wcfg == NULL || out_sz < sizeof(msg)) {
        return -1;
    }
    msg.cores_be = htonl((uint32_t)wcfg->max_cores);
    memcpy(out, &msg, sizeof(msg));
    *out_len = sizeof(msg);
    return 0;
}

static int cb_execute_task(const uint8_t *task_payload,
                           size_t task_payload_len,
                           uint8_t *result_payload,
                           size_t result_payload_sz,
                           size_t *result_payload_len,
                           uint8_t *error_payload,
                           size_t error_payload_sz,
                           size_t *error_payload_len,
                           void *user_ctx) {
    task_msg_t task;
    result_msg_t out;
    int id;
    double a;
    double b;
    long n;
    int threads;
    double val;
    const worker_cfg_t *wcfg = (const worker_cfg_t *)user_ctx;

    if (task_payload == NULL || task_payload_len != sizeof(task) || result_payload == NULL ||
        result_payload_len == NULL || error_payload == NULL || error_payload_len == NULL || wcfg == NULL) {
        return -1;
    }
    if (result_payload_sz < sizeof(out)) {
        return -1;
    }
    memcpy(&task, task_payload, sizeof(task));
    id = (int)ntohl(task.id_be);
    a = be64_to_double(task.a_be);
    b = be64_to_double(task.b_be);
    n = (long)(int64_t)be64_to_host(task.n_be);
    threads = (int)ntohl(task.threads_be);
    if (threads < 1) {
        threads = 1;
    }
    if (threads > wcfg->max_cores) {
        threads = wcfg->max_cores;
    }
    val = integrate_trapz(a, b, n, threads);

    out.id_be = htonl((uint32_t)id);
    out.value_be = double_to_be64(val);
    memcpy(result_payload, &out, sizeof(out));
    *result_payload_len = sizeof(out);
    *error_payload_len = 0U;
    (void)error_payload;
    (void)error_payload_sz;
    return 0;
}

worker_ops_t integral_worker_ops(void) {
    worker_ops_t ops;
    ops.build_hello = cb_build_hello;
    ops.execute_task = cb_execute_task;
    ops.user_ctx = NULL;
    return ops;
}

