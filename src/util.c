#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "straph.h"


int rw_spinlock_rlock(rw_spinlock l){
    int err;

    /* Lock r */
    if ((err = pthread_spin_lock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    /* Lock w if this is the first reader */
    if (++l.n_r == 1){
        if ((err = pthread_spin_lock(&l.w)) != 0){
            pthread_spin_unlock(&l.r);
            errno = err;
            return -1;
        }
    }

    /* Unlock r */
    if ((err = pthread_spin_unlock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    return 0;
}

int rw_spinlock_runlock(rw_spinlock l){
    int err;

    /* Lock r */
    if ((err = pthread_spin_lock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    /* Unlock w if this is the last reader */
    if (--l.n_r == 0){
        if ((err = pthread_spin_unlock(&l.w)) != 0){
            pthread_spin_unlock(&l.r);
            errno = err;
            return -1;
        }
    }

    /* Unlock r */
    if ((err = pthread_spin_unlock(&l.r)) != 0){
        errno = err;
        return -1;
    }

    return 0;
}
    

int rw_spinlock_wlock(rw_spinlock l){
    /* Lock w */
    int err;
    if ((err = pthread_spin_lock(&l.w)) != 0){
        errno = err;
        return -1;
    }
    return 0;
}

int rw_spinlock_wunlock(rw_spinlock l){
    /* Unlock w */
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




void lf_init(struct linked_fifo* lf){
    lf->first = NULL;
    lf->last= NULL;
}
 

/**
 * Adds a new element at the top of the list
 * @param lf A pointer to a linked fifo
 * @param el A pointer to the element to add
 * @return 0 in case of success, -1 otherwise
 */ 
int lf_push(struct linked_fifo* lf, void* el){
    struct lf_cell* new = malloc(sizeof(struct lf_cell));
    if (new == NULL) return -1;

    new->element = el;
    new->next = NULL;

    if (lf->first == NULL) lf->first = new;
    if (lf->last  != NULL) lf->last->next = new;

    lf->last = new;

    return 0;
}

/**
 * Pop an element from the end of a list
 * @param lf A pointer to a linked fifo
 * @return The last element of the list, of NULL in case of error
 */
void* lf_pop(struct linked_fifo* lf){
    void *el;
    struct lf_cell* f = lf->first;
    if (f == NULL){
        errno = ENOENT;
        return NULL;
    }

    lf->first = f->next;
    if (lf->last == f) lf->last = f->next;

    el = f->element;
    free(f);

    return el;
}





void lf_drop(struct linked_fifo* lf){

    struct lf_cell *c1,*c2;
    
    c1 = lf->first;

    while (c1 != NULL){
        c2 = c1->next;
        free(c1);
        c1 = c2;
    }
   
    lf_init(lf);
}