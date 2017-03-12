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
    ssize_t freedsize;
    size_t of_ck, end;
  
    freedsize = 0; 
    of_ck = cb->data_start;
    end   = (cb->data_start+cb->data_size) % cb->sizebuf;

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_ckcount))

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

        /* TODO look for eventual cleaning */
        PTH_ERRCK_NC(pthread_cond_wait(&cb->cond_free, 
                        &cb->lock_ckcount))

    }

    PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_ckcount))

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

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_refs))

    cb->data_start = (cb->data_start + nbyte) % cb->sizebuf;
    cb->data_size -=  nbyte;

    PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_refs))

    return 0;
}

/* Update: used += space_used, notify */
ssize_t cb_acquire(struct c_buf *cb, size_t nbyte){

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_refs))

    cb->data_size +=  nbyte;

    PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_refs))
    PTH_ERRCK_NC(pthread_cond_broadcast(&cb->cond_free))

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

    return size_written+SIZE_CKHEAD*(size_written+MAX_CKSIZE-1)/MAX_CKSIZE; 
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
        if (size_written >= nbyte) break;

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


/* Read as much as possible from the cache
 * update the cache
 * return the size read
 */
size_t inc_cacheread(struct inslot_c* in, void* buf, size_t nbyte){
    size_t size2read, linear_size;
    
    /* Size that will be read from the cache */
    size2read = MIN(in->size_cdata, nbyte);
    
    /* First contiguous read */
    linear_size = MIN(SIZE_CACHE - in->of_cdata, size2read);
    memcpy(buf, &in->cache[in->of_cdata], linear_size);

    /* Update cache */
    in->of_cdata = (in->of_cdata + linear_size) % SIZE_CACHE;

    if (linear_size < size2read){
        /* Second read on contiguous memory */
        buf = (char*) buf + linear_size;
        memcpy(buf, &in->cache[in->of_cdata], size2read-linear_size);
        in->of_cdata = (in->of_cdata + linear_size) % SIZE_CACHE;
    }

    in->size_cdata -= size2read;
    
    return size2read;
}



    
size_t cb_read
(struct c_buf *cb, struct inslot_c *in, 
 size_t of_end, void *buf, size_t nbyte){

    size_t size_read;   /* Total data read from cb */
    size_t linear_size; /* Size of the next contiguos read */
    size_t of_ckend;    /* End of the current chunk */
    cksize_t cksize;    /* Size of the current chunk */
    
    size_read = 0;

    while (size_read < nbyte && in->of_ck != of_end){

        /* Calculate end of the current chunk */
        cksize   = cb_getcksize(cb,in->of_ck);
        of_ckend = (in->of_ck + cksize + SIZE_CKHEAD) % cb->sizebuf;

        /*** First read on contiguous memory ***/

        /* Check if the rest of the ck reach the end of the cb */
        if (in->of_read <= of_ckend){
            /* Can read all the remaining ck */
            linear_size = of_ckend - in->of_read;
        } else {
            /* Read first contiguos part of the ck */
            linear_size = cb->sizebuf - in->of_read;
        }

        /* Limited by the space available on the buffer */
        linear_size = MIN(linear_size,nbyte-size_read);
        memcpy(&((char*)buf)[size_read], 
               &cb->buf[in->of_read], linear_size);

        /* Update size read data and offset unread data */
        size_read += linear_size;
        in->of_read = (in->of_read + linear_size) % cb->sizebuf;

        if (in->of_read == 0 && size_read < nbyte){
            /*** Second read on contiguous memory ***/

            linear_size = MIN(of_ckend,nbyte-size_read);
            memcpy(&((char*)buf)[size_read], 
                   &cb->buf[0], linear_size);

            size_read += linear_size;
            in->of_read = (in->of_read + linear_size) % cb->sizebuf;
        }
  
        /* If we read all the chunk point to the next one */ 
        if (in->of_read == of_ckend){
            in->of_ck = of_ckend;
            in->of_read = (in->of_ck + SIZE_CKHEAD) % cb->sizebuf;
        }
    }

    return size_read;
}


void cb_icc(struct c_buf *cb, size_t of_startck, size_t of_endck){

    (void) cb;
    (void) of_startck;
    (void) of_endck;
    return;
}




ssize_t st_cbread(struct inslot_c* in, void* buf, size_t nbyte){

    ssize_t of_end;     /* End of the readable data on the cb */
    struct c_buf *cb;   /* Shortcut to the circular buffer */
    size_t of_startck,size_read;
    
    /* Read from cache */
    size_read = inc_cacheread(in,buf,nbyte);
    if (size_read >= nbyte) return size_read; 

    /* Read from buffer (nbyte-size_read) + SIZE_CACHE bytes */
    
    cb = in->src->buf; 

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_refs))

    while (1){


        of_end = (cb->data_start + cb->data_size) % cb->sizebuf;

        PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_refs))

        size_read += cb_read(cb, in, of_end, buf, nbyte-size_read);

        if (size_read < nbyte) break;

        /* Atomically increment cnt on chuncks and wait*/
        PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_ckcount))

        cb_icc(cb, of_startck, in->of_ck);
        of_startck = in->of_ck;

        PTH_ERRCK(pthread_mutex_lock(&cb->lock_refs),
                  pthread_mutex_unlock(&cb->lock_ckcount);)
        PTH_ERRCK(pthread_mutex_unlock(&cb->lock_ckcount),
                  pthread_mutex_unlock(&cb->lock_refs);)
        PTH_ERRCK(pthread_cond_broadcast(&cb->cond_free),
                  pthread_mutex_unlock(&cb->lock_refs);)
        PTH_ERRCK(pthread_cond_wait(&cb->cond_acquire, &cb->lock_refs),
                  pthread_mutex_unlock(&cb->lock_refs);)
    }

    in->size_cdata = cb_read(cb, in, of_end, buf, nbyte-size_read);
    size_read += in->size_cdata;
    
    cb_icc(cb, of_startck, in->of_ck);


    return size_read; 
}




ssize_t st_lbwrite(struct out_buf* ob, const void* buf, size_t nbyte){

    /* Linear buffer */
    struct l_buf *lb = ob->buf;
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
    PTH_ERRCK_NC(pthread_mutex_lock(&lb->mutex))

    lb->of_empty += write_size; /* Update */

    PTH_ERRCK_NC(pthread_mutex_unlock(&lb->mutex))

    /* Signal new available data */
    PTH_ERRCK_NC(pthread_cond_broadcast(&lb->cond))
    
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

    PTH_ERRCK_NC(pthread_mutex_unlock(&lb->mutex))

    /* Awake every waiting reader  */
    PTH_ERRCK_NC(pthread_cond_broadcast(&lb->cond)) 
    
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
    PTH_ERRCK_NC(pthread_mutex_lock(&lb->mutex))
    
    /* Wait condition */
    while (lb->of_empty - in->of_start < nbyte &&
           lb->status != BUF_INACTIVE          ){


        PTH_ERRCK(pthread_cond_wait(&lb->cond, &lb->mutex),
                  pthread_mutex_unlock(&lb->mutex);
                  perror("Error:");)
    }

    if (lb->status == BUF_INACTIVE){
       nbyte = lb->of_empty - in->of_start;
    }

    /* Unlock access */
    PTH_ERRCK_NC(pthread_mutex_unlock(&lb->mutex))

    /* Perform read */
    memcpy(buf, &lb->buf[in->of_start], nbyte);
    in->of_start += nbyte;

    return nbyte; 
}



ssize_t st_read(node n, unsigned int slot, void* buf, size_t nbyte){
    struct out_buf* ob;

    if (n->nb_inslots <= slot        ||
        n->inslots[slot] == NULL ){
        return 0;
    }

    /* Get out buffer */
    ob = ((struct inslot*) n->inslots[slot])->src;
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
        errno = err;
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
    PTH_ERRCK_NC(pthread_mutex_destroy(&b->mutex))
    PTH_ERRCK_NC(pthread_cond_destroy(&b->cond))

    free(b->buf);
    free(b);
    return 0;
}

int st_destroycb(struct c_buf* b){
    free(b->buf);

    PTH_ERRCK_NC(pthread_mutex_destroy(&b->lock_refs))
    PTH_ERRCK_NC(pthread_mutex_destroy(&b->lock_ckcount))
    /* TODO propertly destroy cb */
    
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
