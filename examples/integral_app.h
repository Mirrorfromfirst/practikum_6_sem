#ifndef INTEGRAL_APP_H
#define INTEGRAL_APP_H

#include "distr.h"

#include <stdint.h>

typedef struct {
    double a;
    double b;
    long n;
} integral_job_t;

typedef struct {
    integral_job_t job;
    int required_workers;
    int *worker_cores;
    int total_cores;
    int prefix_cores;
    long assigned_n;
    double next_left;
    double total;
} integral_manager_ctx_t;

int integral_manager_ctx_init(integral_manager_ctx_t *ctx, int required_workers, integral_job_t job);
void integral_manager_ctx_free(integral_manager_ctx_t *ctx);

manager_ops_t integral_manager_ops(integral_manager_ctx_t *ctx);
worker_ops_t integral_worker_ops(void);

uint64_t integral_now_ms(void);
double integrate_trapz(double a, double b, long n, int threads);

#endif

