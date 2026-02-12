#include "distr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <workers> <host> <port> --a <A> --b <B> --n <N> [--timeout <sec>]\n", argv0);
}

int main(int argc, char **argv) {
    manager_cfg_t mcfg;
    job_cfg_t job;
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
    return run_manager(&mcfg, &job);
}

