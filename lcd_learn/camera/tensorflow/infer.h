// infer.h
#ifndef INFER_H
#define INFER_H 

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * C function to call
 */
int model_init(void);
int label_init(void);
int infer_run(uint8_t *image);

#ifdef __cplusplus
}
#endif

#endif