#ifndef _COMMON_H_
#define _COMMON_H_

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "linked_fifo.h"

typedef enum {false,true} bool;
#define MIN(x,y) (((x) < (y)) ? (x) : (y))

/* Error check posix threads */
#define PTH_ERRCK(fun_call,cleaning)  \
{                                     \
    int _err = (fun_call);            \
    if (_err != 0) {                  \
      cleaning                        \
      errno = _err;                   \
      return -1;                      \
    }                                 \
}

/* Error check posix threads without cleaning */
#define NOARG
#define PTH_ERRCK_NC(fun_call) PTH_ERRCK(fun_call,NOARG)

#endif
