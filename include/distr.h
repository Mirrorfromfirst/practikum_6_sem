#ifndef DISTR_H
#define DISTR_H

#include <stddef.h>
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
    int (*build_hello)(uint8_t *out,
                       size_t out_sz,
                       size_t *out_len,
                       const worker_cfg_t *wcfg,
                       void *user_ctx);
    int (*execute_task)(const uint8_t *task_payload,
                        size_t task_payload_len,
                        uint8_t *result_payload,
                        size_t result_payload_sz,
                        size_t *result_payload_len,
                        uint8_t *error_payload,
                        size_t error_payload_sz,
                        size_t *error_payload_len,
                        void *user_ctx);
    void *user_ctx;
} worker_ops_t;

typedef struct {
    int (*on_worker_hello)(int worker_index, const uint8_t *hello_payload, size_t hello_payload_len, void *user_ctx);
    int (*build_task)(int worker_index,
                      uint8_t *task_payload,
                      size_t task_payload_sz,
                      size_t *task_payload_len,
                      void *user_ctx);
    int (*on_worker_result)(int worker_index, const uint8_t *result_payload, size_t result_payload_len, void *user_ctx);
    void *user_ctx;
} manager_ops_t;

int run_manager(const manager_cfg_t *mcfg, const manager_ops_t *ops);

int run_worker(const worker_cfg_t *wcfg, const worker_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif

