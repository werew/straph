#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include "straph.h"

typedef enum{false,true} bool;


inline ckcount_t cb_getckcount(struct c_buf *cb, unsigned int of_ck){
    ckcount_t s;
    CB_READUI16(cb,of_ck,&s);
    return s;
}


inline cksize_t cb_getcksize(struct c_buf *cb, unsigned int of_ck){
    cksize_t s;
    CB_READUI16(cb,of_ck+2,&s);
    return s;
}

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

/* Note: Does not update of_lastck */
/* Note: Does not check for free space*/
/* Note: Supposes that bufsize > nbyte */
inline void cb_writechunk
(struct c_buf *cb, size_t offset, const void *buf, cksize_t nbyte){

    cksize_t linear_size;
    ckcount_t count;

    /* Write header */
    count = 0;
    CB_WRITEUI16(cb, offset, &count);
    CB_WRITEUI16(cb, offset+sizeof(ckcount_t), &nbyte);

    /* Calculate data offset */
    offset = (offset + SIZE_CKHEAD) % cb->sizebuf;

    /* First write on contiguous memory */
    linear_size = MIN(cb->sizebuf-offset,nbyte);
    memcpy(&cb->buf[offset], buf, linear_size);

    if ( linear_size < nbyte){
        /* Second write on contiguous memory */
        buf = (char*) buf + linear_size;
        offset = (offset + linear_size) % cb->sizebuf;
        memcpy(&cb->buf[offset], buf, nbyte-linear_size);
    }
}


ssize_t cb_genfreespace
(struct c_buf *cb, ckcount_t maxreads, bool blocking){
    int err;
    ssize_t freedsize;
    size_t of_ck, end;
  
    freedsize = 0; 
    of_ck = cb->data_start;
    end   = (cb->data_start+cb->data_size) % cb->sizebuf;

    if ((err = pthread_mutex_lock(&cb->lock_ckcount)) != 0){
        errno = err;
        return -1;
    }

    while (1){

        while (1){
            ckcount_t ckcount;
            cksize_t cksize;

            /* Check ck read count */
            CB_READUI16(cb,of_ck,&ckcount);
            if (ckcount < maxreads) break;

            /* Consider the total size of the ck as free */ 
            freedsize += SIZE_CKHEAD + cksize;

            /* Move to next ck */
            CB_READUI16(cb,of_ck+sizeof(ckcount_t),&cksize);
            of_ck = (of_ck + SIZE_CKHEAD+cksize) % cb->sizebuf;
            
            /* Stop, ck finished */
            if (of_ck == end) break; 
        }

        if ( blocking == false || freedsize != 0) break;

        pthread_cond_wait(&cb->cond_free, &cb->lock_ckcount);

    }

    if ((err = pthread_mutex_unlock(&cb->lock_ckcount)) != 0){
        errno = err;
        return -1;
    }

    return freedsize;
}



/*
  Subtracting the size of the headers to obtain
  the amount of space available for data transmission
*/
inline size_t cb_realfreespace(size_t free_space){
    size_t min_chunks;

    if (free_space <= SIZE_CKHEAD) return 0;
    min_chunks = (free_space+MAX_CKSIZE-1) / MAX_CKSIZE;
    return free_space - SIZE_CKHEAD*min_chunks;
}


/* Update: start+=new_freespace, used-=new_freespace */
ssize_t cb_release(struct c_buf *cb, size_t nbyte){
    int err;

    if ((err = pthread_mutex_lock(&cb->lock_refs)) != 0){
        errno = err;
        return -1;
    }

    cb->data_start = (cb->data_start + nbyte) % cb->sizebuf;
    cb->data_size -=  nbyte;

    if ((err = pthread_mutex_unlock(&cb->lock_refs)) != 0){
        errno = err;
        return -1;
    }

    return 0;
}

/* Update: used += space_used, notify */
ssize_t cb_acquire(struct c_buf *cb, size_t nbyte){
    int err;

    if ((err = pthread_mutex_lock(&cb->lock_refs)) != 0){
        errno = err;
        return -1;
    }

    cb->data_size +=  nbyte;

    if ((err = pthread_mutex_unlock(&cb->lock_refs))    != 0 ||
        (err = pthread_cond_broadcast(&cb->cond_free)) != 0 ){
        errno = err;
        return -1;
    }

    return 0;
}

/**
 * @brief writes directly data into cb (as chunks) without
 *        any previous check (the space is supposed to be free)
 * @return the offset of the last chunk written 
 */
size_t cb_dowrite(struct c_buf *cb, size_t of_start, const void *buf, size_t nbyte){

    size_t size_written = 0;
    cksize_t size_chunk = 0;

    while (nbyte > size_written){

        /* Write nbyte bytes chunk by chunk */
        size_chunk = MIN(MAX_CKSIZE, nbyte-size_written);
        cb_writechunk(cb, of_start+size_written, buf, size_chunk);

        buf = (char*) buf + size_chunk;
        size_written += size_chunk; 
    }

    return (of_start+size_written-size_chunk) % cb->sizebuf; 
}







ssize_t st_cbwrite(struct out_buf *ob, const void *buf, size_t nbyte){

    /* Circular buffer */
    struct c_buf *cb = ob->buf;

    unsigned int of_start;
    size_t total_freespace, real_freespace;
    ssize_t new_freespace;
    size_t size_written, space_used;

    /* Amout of data transferred to cb */
    size_written = 0;

    /* Offset from where we will start writing */
    of_start = (cb->data_start+cb->data_size) % cb->sizebuf;

    /* Get available free space */
    total_freespace = cb->sizebuf - cb->data_size;
    real_freespace  = cb_realfreespace(total_freespace);

    /* If the space is not enough try to free some more */
    if (real_freespace < nbyte){
       
        /* If there isn't free space at all it's ok to wait */
        bool blocking = (real_freespace == 0);
        if ((new_freespace = cb_genfreespace
                (cb,ob->nreaders, blocking)) == -1) return -1;

        /* Update values if new space is available */
        if (new_freespace > 0){
            total_freespace += new_freespace;
            real_freespace = cb_realfreespace(total_freespace);
            if (cb_release(cb, new_freespace) == -1) return -1; 
        }
    }

    
    while (1){

        /*
          Write as much data as possible: the size of the 
          write is limited by real_freespace 
        */ 
        size_t size_write = MIN(real_freespace, nbyte-size_written);
        space_used = cb_dowrite(cb, of_start, 
            (char*) buf + size_written, size_write);
        of_start += space_used; total_freespace -= space_used;

        /* Stop writing if we wrote nbyte of data */
        size_written += size_write;
        if (size_written < nbyte) break;

        /* Update cb and notify new data */
        if (cb_acquire(cb, space_used) == -1) return -1; 

        /* Free more data, waiting if necessary */
        if ((new_freespace = cb_genfreespace
                (cb,ob->nreaders, true)) == -1  ||
             cb_release(cb, new_freespace) == -1 ) return -1;

        /* Update free space count */
        total_freespace += new_freespace;
        real_freespace  = cb_realfreespace(total_freespace);
    }

    return size_written;
}









ssize_t st_lbwrite(struct out_buf* ob, const void* buf, size_t nbyte){

    /* Linear buffer */
    struct l_buf *lb = ob->buf;

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
            return st_lbwrite(&n->outslots[slot],
                       buf, nbyte);
        case CIR_BUF: 
            return st_cbwrite(&n->outslots[slot],
                       buf, nbyte);
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


/* TODO initialize */
struct c_buf* st_makecb(size_t sizebuf){
    int err;
    struct c_buf* b;

    b = malloc(sizeof(struct c_buf));
    if (b == NULL) return NULL;

    if ((err = pthread_mutex_init(&b->lock_refs,
                    NULL)) != 0){
        free(b);
        errno = err;
        return NULL;
    }

    if ((err = pthread_mutex_init(&b->lock_ckcount,
                    NULL)) != 0){
        pthread_mutex_destroy(&b->lock_refs);
        free(b);
        errno = err;
        return NULL;
    }

    b->buf = malloc(sizebuf);
    if (b->buf == NULL) {
        err = errno;
        pthread_mutex_destroy(&b->lock_refs);
        pthread_mutex_destroy(&b->lock_ckcount);
        free(b);
        errno = err;
        return NULL;
    }

    b->sizebuf = sizebuf;
    b->data_start = 0;
    b->data_size = 0;

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

    err = pthread_mutex_destroy(&b->lock_refs);
    if (err != 0){
        errno = err;
        return -1;
    }

    err = pthread_mutex_destroy(&b->lock_ckcount);
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
