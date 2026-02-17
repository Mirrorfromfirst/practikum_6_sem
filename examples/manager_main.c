#include "distr.h"
#include "integral_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <workers> <host> <port> --a <A> --b <B> --n <N> [--timeout <sec>]\n", argv0);
}

int main(int argc, char **argv) {
    manager_cfg_t mcfg;
    integral_job_t job;
    integral_manager_ctx_t app_ctx;
    manager_ops_t ops;
    uint64_t t0;
    uint64_t t1;
    int rc;
    int i;

    if (argc < 8) {
        usage(argv[0]);
        return 1;
    }
    mcfg.required_workers = atoi(argv[1]);
    mcfg.host = argv[2];
    mcfg.port = argv[3];
    mcfg.max_time_sec = 30;
    job.a = 0.0;
    job.b = 1.0;
    job.n = 100000;

    for (i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--a") == 0 && i + 1 < argc) {
            job.a = atof(argv[++i]);
        } else if (strcmp(argv[i], "--b") == 0 && i + 1 < argc) {
            job.b = atof(argv[++i]);
        } else if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) {
            job.n = atol(argv[++i]);
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            mcfg.max_time_sec = atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (integral_manager_ctx_init(&app_ctx, mcfg.required_workers, job) != 0) {
        return 2;
    }
    ops = integral_manager_ops(&app_ctx);
    t0 = integral_now_ms();
    rc = run_manager(&mcfg, &ops);
    t1 = integral_now_ms();
    if (rc == 0) {
        printf("INTEGRAL=%.12f\n", app_ctx.total);
        printf("TOTAL_TIME_SEC=%.6f\n", (double)(t1 - t0) / 1000.0);
        printf("TOTAL_CORES=%d\n", app_ctx.total_cores);
    }
    integral_manager_ctx_free(&app_ctx);
    return rc;
}

