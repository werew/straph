#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include "straph.h"



typedef enum{false,true} bool;
#define MIN(x,y) (((x) < (y)) ? (x) : (y))



/**
 * @brief get the count field of a chunk (i.e. number of times
 *        the chunk was read)
 * @param cb Pointer to a circular buffer
 * @param of_ck Offset to the chunk
 * @return The number of times the chunk was read
 */
inline ckcount_t cb_getckcount(struct c_buf *cb, unsigned int of_ck){
    ckcount_t s;
    CB_READUI16(cb,of_ck,&s);
    return s;
}


/**
 * @brief get the size field of a chunk 
 * @param cb Pointer to a circular buffer
 * @param of_ck Offset to the chunk
 * @return Size of the chunk in bytes
 */
inline cksize_t cb_getcksize(struct c_buf *cb, unsigned int of_ck){
    cksize_t s;
    CB_READUI16(cb,of_ck+2,&s);
    return s;
}


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



/**
 * @brief Calculate the space occupied by chunks
 *        that reached maxreads reads
 * @param cb Circular buffer
 * @param maxreads Number of reads beyond what
 *        a chunk has to be considered as free
 * @param blocking If true wait for free chunks 
 *        if none is currently available
 * @return The size occupied by releasable chunks 
 *         or -1 in case of error
 */
ssize_t cb_releasable 
(struct c_buf *cb, ckcount_t maxreads, bool blocking){
    ssize_t freedsize;
    size_t of_ck, end;
  
    freedsize = 0; 

    /* No need to lock the references when a writer
       is reading them */
    of_ck = cb->ref_datatransf % cb->sizebuf;
    end   = cb->ref_datawritten % cb->sizebuf;

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_ckcount))

    while (1){

        while (1){
            ckcount_t ckcount;
            cksize_t cksize;

            /* Check ck read count */
            CB_READUI16(cb,of_ck,&ckcount);
            if (ckcount < maxreads) break;

            /* Consider the total size of the ck as free */ 
            CB_READUI16(cb,of_ck+sizeof(ckcount_t),&cksize);
            freedsize += SIZE_CKHEAD + cksize;

            /* Move to next ck */
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



/**
 * @brief Calculate the effective space available for
 *        data transfer by calculation the number of chunks 
 *        needed and subtracting the size of the headers
 * @param free_space Total free space       
 * @return Effective free space for data transfer
 */
inline size_t cb_realfreespace(size_t free_space){
    size_t min_chunks;

    if (free_space <= SIZE_CKHEAD) return 0;
    min_chunks = (free_space+MAX_CKSIZE-1) / MAX_CKSIZE;
    return free_space - SIZE_CKHEAD*min_chunks;
}


/**
 * @brief Release space previously used. The space released
 *        is available for use. In other words, reduces the size
 *        of the space used
 * @param cb Circular buffer from where release the space
 * @param nbyte Number of bytes to release
 * @return 0 in case of success, -1 otherwise
 * TODO change return type
 */
ssize_t cb_release(struct c_buf *cb, size_t nbyte){

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_refs))

    cb->ref_datatransf += nbyte;

    PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_refs))

    return 0;
}

/**
 * @brief Declare some space as being used. Waiting readers
 *        are notified when new space is acquired.
 * @param cb Circular buffer in object
 * @param nbyte Number of bytes to acquire
 * @return 0 in case of success, -1 otherwise
 * TODO change return type
 */
ssize_t cb_acquire(struct c_buf *cb, size_t nbyte){

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_refs))

    cb->ref_datawritten +=  nbyte;

    PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_refs))
    PTH_ERRCK_NC(pthread_cond_broadcast(&cb->cond_acquire))

    return 0;
}

/**
 * @brief writes directly data into cb (as chunks) without
 *        any previous check (the space is supposed to be free)
 * @return space used
 */
size_t cb_dowrite(struct c_buf *cb, size_t of_start, const void *buf, size_t nbyte){

    size_t size_written = 0;
    cksize_t size_chunk = 0;
    if (nbyte == 0) return 0;

    while (nbyte > size_written){

        /* Write nbyte bytes chunk by chunk */
        size_chunk = MIN(MAX_CKSIZE, nbyte-size_written);
        cb_writechunk(cb, of_start+size_written, buf, size_chunk);

        buf = (char*) buf + size_chunk;
        size_written += size_chunk; 
    }

    return size_written+SIZE_CKHEAD*(size_written+MAX_CKSIZE-1)/MAX_CKSIZE; 
}






/**
 * @brief Writes data to a circular buffer
 * @param
 * @param
 * @return
 */
ssize_t st_cbwrite
(struct c_buf *cb, unsigned int nreaders, const void *buf, size_t nbyte){

    unsigned int of_start;
    size_t total_freespace, real_freespace;
    ssize_t new_freespace;
    size_t size_written, space_used;

    /* Amout of data transferred to cb */
    size_written = 0;

    /* No need to lock the references when a writer
       is reading them */

    /* Offset from where we will start writing */
    of_start = cb->ref_datawritten % cb->sizebuf;

    /* Get available free space */
    total_freespace = cb->sizebuf - (cb->ref_datawritten - cb->ref_datatransf);
    real_freespace  = cb_realfreespace(total_freespace);

    /* If the space is not enough try to free some more */
    if (real_freespace < nbyte){
       
        /* If there isn't free space at all it's ok to wait */
        bool blocking = (real_freespace == 0);
        if ((new_freespace = cb_releasable(cb, nreaders, blocking)) == -1) 
            return -1;

        /* Update values if new space is available */
        if (new_freespace > 0){
            total_freespace += new_freespace;
            real_freespace = cb_realfreespace(total_freespace);
            /* XXX is release necessary ? */
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

        /* Update cb and notify new data */
        if (cb_acquire(cb, space_used) == -1) return -1; 

        /* Stop writing if we wrote nbyte of data */
        size_written += size_write;
        if (size_written >= nbyte) break;

        /* Free more data, waiting if necessary */
        if ((new_freespace = cb_releasable(cb, nreaders, true)) == -1 ||
            /* XXX is release necessary ? */
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
    if (size2read == 0) return 0;
    
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




struct cb_transf cb_read
(struct c_buf *cb, size_t data_av, 
 struct inslot_c *in, void *buf, size_t nbyte){

/* BIG TODO: naming !!!!! */

    size_t linear_size; /* Size of the next contiguos read */
    size_t of_ckend;    /* End of the current chunk */
    size_t of_read;
    cksize_t cksize;    /* Size of the current chunk */
    
    struct cb_transf tr = {0,0,0};
    of_read = in->data_read % cb->sizebuf;

    while (tr.data_size < nbyte && tr.real_size < data_av){

        /* Calculate end of the current chunk */
        cksize   = cb_getcksize(cb,in->of_ck);
        of_ckend = (in->of_ck + cksize + SIZE_CKHEAD) % cb->sizebuf;

        /* If we are at the beginning of the chunk, consider the header as read */
        if (of_read == in->of_ck){
            of_read = (in->of_ck + SIZE_CKHEAD) % cb->sizebuf;
            tr.real_size += SIZE_CKHEAD;
        }

        /*** First read on contiguous memory ***/

        /* Check if the rest of the ck reach the end of the cb */
        if (of_read <= of_ckend){
            /* Can read all the remaining ck */
            linear_size = of_ckend - of_read;
        } else {
            /* Read first contiguos part of the ck */
            linear_size = cb->sizebuf - of_read;
        }

        /* Limited by the space available on the buffer */
        linear_size = MIN(linear_size,nbyte-tr.data_size);


        memcpy(&((char*)buf)[tr.data_size], 
               &cb->buf[of_read], linear_size);
    

        /* Update size read data and offset unread data */
        tr.data_size += linear_size;
        tr.real_size += linear_size;
        of_read = (of_read + linear_size) % cb->sizebuf;

        if (of_read == 0 && tr.data_size < nbyte){
            /*** Second read on contiguous memory ***/

            linear_size = MIN(of_ckend,nbyte-tr.data_size);
            memcpy(&((char*)buf)[tr.data_size], 
                   &cb->buf[0], linear_size);
            tr.data_size += linear_size;
            tr.real_size += linear_size;
            of_read = (of_read + linear_size) % cb->sizebuf;
        }
  
        /* If we read all the chunk point to the next one */ 
        if (of_read == of_ckend){
            in->of_ck = of_ckend;
            tr.cks_passed += 1;
        }
    }

    in->data_read += tr.real_size;

    return tr;
}


/* Increment cnt chunks from of_startck to of_endck excluded */
int isc_icc(struct inslot_c* isc, size_t of_startck, unsigned int ncks){

    int freed;
    ckcount_t cnt;
    cksize_t  sizeck;
    struct c_buf *cb;
    unsigned int i;


    freed = 0;
    cb = isc->src->buf;

    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_ckcount))

    for (i = 0; i < ncks; i++){
        /* Increment count of current chunk */
        cnt  = cb_getckcount(cb,of_startck) + 1;
        CB_WRITEUI16(cb,of_startck,&cnt)

        if (cnt >= isc->src->nreaders) freed++;
       
        /* Go to the nex chunk */ 
        sizeck = cb_getcksize(cb,of_startck);
        of_startck = (of_startck+SIZE_CKHEAD+sizeck) % cb->sizebuf; 
    }

    PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_ckcount))

    if (freed > 0){
        PTH_ERRCK_NC(pthread_cond_broadcast(&cb->cond_free));
    }

    return freed;
}


size_t isc_getavailable(struct inslot_c *in){
    struct c_buf *cb = in->src->buf;
    size_t data_available;

    /* Wait for new data if necessary */
    PTH_ERRCK_NC(pthread_mutex_lock(&cb->lock_refs))
        while (in->data_read >= cb->ref_datawritten){
            PTH_ERRCK(pthread_cond_wait(&cb->cond_acquire, &cb->lock_refs), 
                      pthread_mutex_unlock(&cb->lock_refs);)
        }

        data_available = cb->ref_datawritten - in->data_read;
    PTH_ERRCK_NC(pthread_mutex_unlock(&cb->lock_refs))

    return data_available;
}







ssize_t st_cbread(struct inslot_c* in, void* buf, size_t nbyte){

    struct cb_transf tr; /* Transfer infos */
    ssize_t data_av;     /* Unread data available on the buffer */
    struct c_buf *cb;    /* Shortcut to the circular buffer */
    size_t of_startck;   /* First ck of each read (used for cb_icc) */ 
    size_t size_read;    /* Total size that was read */
    unsigned int cks_passed; /* Chunks completed */

    /* Read from cache */
    size_read = inc_cacheread(in,buf,nbyte);
    if (size_read >= nbyte) return size_read; 

    /* Read from buffer */
    cb = in->src->buf;
    while (1){

        /* Get size of data ready to be read */
        data_av = isc_getavailable(in);

        /* Transfer data to user's buffer */
        of_startck = in->of_ck; 
        tr = cb_read(cb, data_av, in, 
             &((char*)buf)[size_read], nbyte-size_read);

        /* Update */
        data_av -= tr.real_size;
        size_read += tr.data_size;
        if (size_read >= nbyte) break;

        /* Mark chunks and signals free chunks */
        isc_icc(in, of_startck, tr.cks_passed);
    }

    /* 3 - Transfear remaning data to the cache */
    cks_passed = tr.cks_passed;
    if (data_av > 0){
        tr = cb_read(cb, data_av, in, &in->cache[in->of_cdata], SIZE_CACHE);
        cks_passed += tr.cks_passed;
        in->size_cdata = tr.data_size;
        size_read += tr.data_size;
    }
    
    /* Mark chunks and signals free chunks */
    isc_icc(in, of_startck, cks_passed);

    return size_read; 
}




ssize_t st_lbwrite(struct l_buf *lb, const void* buf, size_t nbyte){

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

    if (n->nb_inslots <= slot){
        errno = EINVAL;
        return -1;
    }

    if (n->inslots[slot] == NULL ) return 0;

    /* Get out buffer */
    ob = ((struct inslot*) n->inslots[slot])->src;
    if (ob == NULL) return 0;

    switch (ob->type){
        case LIN_BUF: 
            return st_readlb(n->inslots[slot], buf, nbyte);
        case CIR_BUF: 
            return st_cbread(n->inslots[slot], buf, nbyte);
        default: 
            errno = EINVAL;
            return -1;
    }
}

ssize_t st_write(node n, unsigned int slot, 
              const void* buf, size_t nbyte){

    
    struct out_buf *ob; /* Target output buffer */
  
    /* Bad slot number */ 
    if (n->nb_outslots <= slot) {
        errno = EINVAL;
        return -1;
    }

    ob = &n->outslots[slot];

    /* 
     If no slot was allocated (user choice) 
    */
    if (ob->buf == NULL ) return 0;

    
    switch (n->outslots[slot].type){
        case LIN_BUF: 
            return st_lbwrite(ob->buf, buf, nbyte);
        case CIR_BUF: 
            return st_cbwrite(ob->buf, ob->nreaders, buf, nbyte);
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


struct c_buf* st_makecb(size_t sizebuf){
    int err;
    struct c_buf* b;

    if ((b = malloc(sizeof(struct c_buf))) == NULL) return NULL;
    if ((b->buf = malloc(sizebuf)) == NULL){
        free(b); return NULL;
    }

    if ((err = pthread_mutex_init(&b->lock_refs,NULL)) != 0) 
        goto error_1;
    if ((err = pthread_mutex_init(&b->lock_ckcount,NULL)) != 0) 
        goto error_2;
    if ((err = pthread_cond_init(&b->cond_free,NULL)) != 0)
        goto error_3;
    if ((err = pthread_cond_init(&b->cond_acquire,NULL)) != 0) 
        goto error_4;

    b->sizebuf = sizebuf;
    b->ref_datatransf  = 0;
    b->ref_datawritten = 0;

    return b;

error_4:
    pthread_cond_destroy(&b->cond_free);
error_3:
    pthread_mutex_destroy(&b->lock_ckcount);
error_2:
    pthread_mutex_destroy(&b->lock_refs);
error_1:
    free(b);
    errno = err;
    return NULL;
}

struct inslot_l* st_makeinslotl(struct out_buf* b){
    struct inslot_l* is = malloc(sizeof(struct inslot_l));
    if (is == NULL) return NULL;

    is->src = b;
    is->of_start = 0;
    return is;
}

struct inslot_c* st_makeinslotc(struct out_buf* b){
    struct inslot_c* is = calloc(1,sizeof(struct inslot_c));
    if (is == NULL) return NULL;

    is->src = b;
    is->data_read = 0;
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
