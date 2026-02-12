#ifndef DISTR_H
#define DISTR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *host;      
    const char *port;    
    int max_cores;         
    int max_time_sec;      
} worker_cfg_t;

typedef struct {
    const char *host;         
    const char *port;          
    int required_workers;      
    int max_time_sec;        
} manager_cfg_t;

typedef struct {
    double a;    
    double b;      
    long n;      
} job_cfg_t;

int run_manager(const manager_cfg_t *mcfg, const job_cfg_t *job);

int run_worker(const worker_cfg_t *wcfg);

double integrate_trapz(double a, double b, long n, int threads, int max_time_sec, int *timed_out);

#ifdef __cplusplus
}
#endif

#endif

