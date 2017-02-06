#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include "straph.h"


ssize_t st_lbwrite(struct l_buf* lb, const void* buf, size_t nbyte){

    int err;
    size_t space_available;
    size_t write_size;

    /* 
     Calculate max write capability 
     Note that the writer has full read-access to 
     of_size because it's the only potential writer 
    */
    space_available = lb->sizebuf-lb->of_empty;
    write_size = (space_available < nbyte)? 
                  space_available : nbyte;

    if (write_size == 0) return nbyte;

    printf("Writing: %ld bytes\n", write_size);

    memcpy(&lb->buf[lb->of_empty], buf, write_size);
    
    /* Update offset */
    if ((err = pthread_mutex_lock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    lb->of_empty += write_size; /* Update */

    if ((err = pthread_mutex_unlock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    /* Signal new available data */
    if ((err = pthread_cond_broadcast(&lb->cond)) != 0) {
        errno = err;
        return -1;
    }
    
    return nbyte; 
}

int st_bufstatlb(struct l_buf* lb, int status){
    /* Update offset */
    int err;
    if ((err = pthread_mutex_lock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    lb->status = status; /* Update */

    if ((err = pthread_mutex_unlock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    /* Awake every waiting reader  */
    if ((err = pthread_cond_broadcast(&lb->cond)) != 0) {
        errno = err;
        return -1;
    }
    
    return 0; 
}

int st_bufstat(node n, unsigned int slot, int status){

    if (n->nb_outslots <= slot               ||
        n->outslots[slot].buf == NULL ){
        errno = ENOENT;
        return -1;
    }

    switch (n->outslots[slot].type){
        case LIN_BUF: 
            return st_bufstatlb(n->outslots[slot].buf, status);
        case CIR_BUF: 
            return 0; 
        default: 
            errno = EINVAL;
            return -1;
    }
}




ssize_t st_readlb(struct inslot_l* in, void* buf, size_t nbyte){
    int err;

    /* Source buffer */
    struct l_buf* lb = in->src->buf;
    
    /* 
     Read the minimum between the requested size and
     the max size of the remaining buffer
    */
    size_t max_read = lb->sizebuf - in->of_start;
    nbyte = (nbyte < max_read)? nbyte : max_read;

    /* Ignore reads of zero bytes */
    if (nbyte == 0) return 0;

    /* Lock access */
    if ((err = pthread_mutex_lock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }
    
    /* Wait condition */
    while (lb->of_empty - in->of_start < nbyte &&
           lb->status != BUF_INACTIVE          ){


        err = pthread_cond_wait(&lb->cond, &lb->mutex);
        if (err != 0){
            pthread_mutex_unlock(&lb->mutex);
            errno = err;
            perror("Error:");
            return -1;
        }
    }

    if (lb->status == BUF_INACTIVE){
       nbyte = lb->of_empty - in->of_start;
    }

    /* Unlock access */
    if ((err = pthread_mutex_unlock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    /* Perform read */
    memcpy(buf, &lb->buf[in->of_start], nbyte);
    in->of_start += nbyte;

    return nbyte; 
}



ssize_t st_read(node n, unsigned int slot, void* buf, size_t nbyte){
    struct inslot_l* islot;
    struct out_buf* ob;

    if (n->nb_inslots <= slot        ||
        n->inslots[slot] == NULL ){
        return 0;
    }

    /* Get out buffer */
    islot = n->inslots[slot];
    ob = islot->src; 

    if (ob == NULL) return 0;

    switch (ob->type){
        case LIN_BUF: 
            return st_readlb(n->inslots[slot], buf, nbyte);
        case CIR_BUF: 
            return 0; 
        default: 
            errno = EINVAL;
            return -1;
    }
}

ssize_t st_write(node n, unsigned int slot, 
              const void* buf, size_t nbyte){

    /* XXX should a node detect NULL buffers ? */

    /* 
     If no slot is available (user choice) 
     act as the write was successful 
    */
    if (n->nb_outslots <= slot               ||
        n->outslots[slot].buf == NULL ){
        return nbyte;
    }

    switch (n->outslots[slot].type){
        case LIN_BUF: 
            return st_lbwrite(n->outslots[slot].buf,
                            buf, nbyte);
        case CIR_BUF: 
            return 0; /*write_cb(n->outslots[slot].buf,
                       buf, nbyte); */
        default: 
            errno = EINVAL;
            return -1;
    }
}




struct l_buf* st_makelb(size_t sizebuf){
    int err;
    struct l_buf* b;

    b = malloc(sizeof(struct l_buf));
    if (b == NULL) return NULL;

    b->buf = malloc(sizebuf);
    if (b->buf == NULL) {
        free(b);
        return NULL;
    }

    b->sizebuf = sizebuf;
    b->of_empty = 0;
    b->status = BUF_READY;

    if ((err = pthread_mutex_init(&b->mutex, NULL)) != 0 ||
        (err = pthread_cond_init(&b->cond, NULL))   != 0 ){
        free(b->buf);
        free(b);
        return NULL;
    }

    return b;
}



struct c_buf* st_makecb(size_t sizebuf){
    int err;
    struct c_buf* b;

    b = malloc(sizeof(struct c_buf));
    if (b == NULL) return NULL;

    if ((err = pthread_mutex_init(&b->access_lock,
                    NULL)) != 0){
        free(b);
        errno = err;
        return NULL;
    }

    if ((err = pthread_mutex_init(&b->nb_readers_mutex,
                    NULL)) != 0){
        pthread_mutex_destroy(&b->access_lock);
        free(b);
        errno = err;
        return NULL;
    }

    b->buf = malloc(sizebuf);
    if (b->buf == NULL) {
        err = errno;
        pthread_mutex_destroy(&b->access_lock);
        pthread_mutex_destroy(&b->nb_readers_mutex);
        free(b);
        errno = err;
        return NULL;
    }

    b->sizebuf = sizebuf;
    b->of_firstck = 0;
    b->of_lastck  = 0;
    b->nb_readers = 0;

    return b;
}

struct inslot_l* st_makeinslotl(struct out_buf* b){
    struct inslot_l* is = malloc(sizeof(struct inslot_l));
    if (is == NULL) return NULL;

    is->src = b;
    is->of_start = 0;
    return is;
}

struct inslot_l* st_makeinslotc(struct out_buf* b){
    struct inslot_l* is = calloc(1,sizeof(struct inslot_l));
    if (is == NULL) return NULL;

    is->src = b;
    return is;
}



int st_destroylb(struct l_buf* b){
    int err;
    if ((err = pthread_mutex_destroy(&b->mutex)) != 0 ||
        (err = pthread_cond_destroy(&b->cond))   != 0 ){
        errno = err;
        return -1;
    }
    free(b->buf);
    free(b);
    return 0;
}

int st_destroycb(struct c_buf* b){
    int err;

    free(b->buf);

    err = pthread_mutex_destroy(&b->access_lock);
    if (err != 0){
        errno = err;
        return -1;
    }

    err = pthread_mutex_destroy(&b->nb_readers_mutex);
    if (err != 0){
        errno = err;
        return -1;
    }
    
    return 0;
}


void* st_makeb(unsigned char buftype, size_t bufsize){

    switch (buftype){
        case CIR_BUF: return st_makecb(bufsize);
        case LIN_BUF: return st_makelb(bufsize);
        default: errno = EINVAL;
                 return NULL;
    }

}

int st_destroyb(struct out_buf *buf){
    switch (buf->type){
        case LIN_BUF: return st_destroylb(buf->buf);
        case CIR_BUF: return st_destroycb(buf->buf);
        default: errno = EINVAL;
                 return -1;  
    }
}
