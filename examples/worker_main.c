#include "distr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s --host <host> --port <port> [--cores N] [--timeout S]\n", argv0);
}

int main(int argc, char **argv) {
    worker_cfg_t wcfg;
    int i;

    wcfg.host = "127.0.0.1";
    wcfg.port = "5555";
    wcfg.max_cores = 1;
    wcfg.max_time_sec = 30;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            wcfg.host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            wcfg.port = argv[++i];
        } else if (strcmp(argv[i], "--cores") == 0 && i + 1 < argc) {
            wcfg.max_cores = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            wcfg.max_time_sec = atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    return run_worker(&wcfg);
}

