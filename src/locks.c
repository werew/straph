#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "straph.h"


int rw_spinlock_rlock(rw_spinlock l){
    int err;

    // Lock r
    if ((err = pthread_spin_lock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    // Lock w if this is the first reader
    if (++l.n_r == 1){
        if ((err = pthread_spin_lock(&l.w)) != 0){
            pthread_spin_unlock(&l.r);
            errno = err;
            return -1;
        }
    }

    // Unlock r
    if ((err = pthread_spin_unlock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    return 0;
}

int rw_spinlock_runlock(rw_spinlock l){
    int err;

    // Lock r
    if ((err = pthread_spin_lock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    // Unlock w if this is the last reader
    if (--l.n_r == 0){
        if ((err = pthread_spin_unlock(&l.w)) != 0){
            pthread_spin_unlock(&l.r);
            errno = err;
            return -1;
        }
    }

    // Unlock r
    if ((err = pthread_spin_unlock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    return 0;
}
    

int rw_spinlock_wlock(rw_spinlock l){
    // Lock w
    int err;
    if ((err = pthread_spin_lock(&l.w)) != 0){
        errno = err;
        return -1;
    }
    return 0;
}

int rw_spinlock_wunlock(rw_spinlock l){
    // Unlock w
    int err;
    if ((err = pthread_spin_unlock(&l.w)) != 0){
        errno = err;
        return -1;
    }
    return 0;
}
    
int rw_spinlock_init(rw_spinlock* l){

    int err;
    if ((err = pthread_spin_init(&l->r, 
               PTHREAD_PROCESS_PRIVATE)) != 0){
        errno = err;
        return -1;
    }

    if ((err = pthread_spin_init(&l->w,
               PTHREAD_PROCESS_PRIVATE)) != 0){
        pthread_spin_destroy(&l->r);
        errno = err;
        return -1;
    }

    l->n_r = 0;

    return 0;
}

int rw_spinlock_destroy(rw_spinlock l){

    int err;
    if ((err = pthread_spin_destroy(&l.r)) != 0 ||
        (err = pthread_spin_destroy(&l.w)) != 0 ){
        errno = err;
        return -1;
    }

    return 0;
}
