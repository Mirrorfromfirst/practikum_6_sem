#define _POSIX_C_SOURCE 200809L
#include "distr.h"
#include "internal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    double a;
    double h;
    long i_begin;
    long i_end;
    uint64_t deadline_ms;
    atomic_int *timed_out;
    double *out_partial;
} thr_ctx_t;

static double f(double x) {
    return 4.0 / (1.0 + x * x);
}

static void *thr_run(void *arg) {
    thr_ctx_t *ctx = (thr_ctx_t *)arg;
    double sum = 0.0;
    long i;

    for (i = ctx->i_begin; i < ctx->i_end; ++i) {
        if (ctx->deadline_ms > 0U && now_ms() > ctx->deadline_ms) {
            atomic_store(ctx->timed_out, 1);
            break;
        }
        if (atomic_load(ctx->timed_out) != 0) {
            break;
        }
        {
            double x1 = ctx->a + (double)i * ctx->h;
            double x2 = x1 + ctx->h;
            sum += 0.5 * (f(x1) + f(x2)) * ctx->h;
        }
    }
    *ctx->out_partial = sum;
    return NULL;
}

double integrate_trapz(double a, double b, long n, int threads, int max_time_sec, int *timed_out) {
    pthread_t *ths = NULL;
    thr_ctx_t *ctxs = NULL;
    double *parts = NULL;
    double res = 0.0;
    atomic_int timeout_flag;
    uint64_t deadline_ms = 0U;
    double h;
    long base, rem;
    int t;

    if (timed_out != NULL) {
        *timed_out = 0;
    }
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
    if (max_time_sec > 0) {
        deadline_ms = now_ms() + (uint64_t)max_time_sec * 1000ULL;
    }
    atomic_init(&timeout_flag, 0);

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
            ctxs[t].deadline_ms = deadline_ms;
            ctxs[t].timed_out = &timeout_flag;
            ctxs[t].out_partial = &parts[t];
            cursor += span;
            (void)pthread_create(&ths[t], NULL, thr_run, &ctxs[t]);
        }
    }
    for (t = 0; t < threads; ++t) {
        (void)pthread_join(ths[t], NULL);
        res += parts[t];
    }

    if (timed_out != NULL) {
        *timed_out = atomic_load(&timeout_flag);
    }
    free(ths);
    free(ctxs);
    free(parts);
    return res;
}

