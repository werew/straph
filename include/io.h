#ifndef _IO_H_
#define _IO_H_

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "straph.h"
#include "linked_fifo.h"
#include "common.h"



/* Buffer status */
#define BUF_READY    0 /* Buf ready to be used:
                          just created or rewinded  */
#define BUF_ACTIVE   1 /* Buf receiving data:
                          node is executing */
#define BUF_INACTIVE 2 /* Not receiving anymore but it
                          still possible to read: node
                          has just terminated its execution */

/**
 * Output buffer container: this is just a wrapper
 * for the different types of output buffers.
 */
struct out_buf {
    unsigned char type;      /* Type of the buffer */
    void* buf;               /* Output buffer */
    unsigned int nreaders;   /* Number of readers actives */
};


/**
 * Linear buffer:
 * A linear buffer provides a finite write and
 * read capability (max sizebuf). The data written
 * out of the capacity of the buffer shall be ignored.
 */
struct l_buf {
    char* buf;             /* Pointer to the buf */
    unsigned int sizebuf;  /* Size of the buf */
    unsigned int of_empty; /* Offset to the unwritten zone */
    
    char status;           /* Indicates if the buf is 
                              receiving data or not   */

    pthread_mutex_t mutex; /* To regulate of_empty access  */
    pthread_cond_t  cond;  /* To signal new available data */
};


/**
 * Circular buffer:
 * A circular buffer provides an unlimited write and read capability. At every
 * write the data is incorporated inside one or more chunks. 
 * Each chunk is so composed:
 *          2 byte      2 byte      n bytes
 *      +------------+-----------+------------+
 *      | read count | size data | data ...   |
 *      +------------+-----------+------------+
 * - read count: indicates how many times the chunk has been read 
 * - size data : size in bytes of the data contained in the chunk
 */
struct c_buf {
    char* buf;                  /* Pointer to the buf */
    unsigned int sizebuf;       /* Size of the buf */

    size_t ref_datawritten;        /* Total data written to buf */
    size_t ref_datatransf;         /* Total data writtan to the buf
                                   and consumed by all readers */

    pthread_mutex_t lock_ckcount;    /* Concurrent reads/writes of the 
                                        count header of the chunks */
    pthread_cond_t  cond_free;

    /* XXX dont need a lock for datatransf (only the writer uses it) */
    pthread_mutex_t lock_refs;       /* Concurrent reads/writes of
                                        ref_datawritten and ref_datatransf */
    pthread_cond_t  cond_acquire;

};


/**
 * Read a 16 bits unsigned int from a circular buffer
 * @param cb Pointer to a circular buffer (struct c_buf*)
 * @param o Offset from where start reading
 * @param a Address where to store the result
 */
#define CB_READUI16(cb,o,a) { ((unsigned char*) a)[0] = cb->buf[(o) % cb->sizebuf];       \
                              ((unsigned char*) a)[1] = cb->buf[((o)+1) % cb->sizebuf];   \
                           }

/**
 * Write a 16 bits unsigned int into a circular buffer
 * @param cb Pointer to a circular buffer (struct c_buf*)
 * @param o Offset from where start writing
 * @param a Address pointing to the data to write 
 */
#define CB_WRITEUI16(cb,o,a) { cb->buf[(o) % cb->sizebuf] = ((unsigned char*) a)[0];      \
                               cb->buf[((o)+1) % cb->sizebuf] = ((unsigned char*) a)[1];  \
                            }


typedef uint16_t ckcount_t; /* Number times a chunk was read */
typedef uint16_t cksize_t ; /* Size of the chunk in bytes */

#define SIZE_CKHEAD (sizeof(ckcount_t)+sizeof(cksize_t))
#define MAX_CKDATASIZE 0xffff /* Max uint16_t */
#define MAX_CKSIZE  (SIZE_CKHEAD + MAX_CKDATASIZE)

/* Header of a chunk */
struct cb_ckhead{
    ckcount_t count; /* Number of reads  */
    cksize_t  size;  /* Size of the data */
};









/***** Input slots *****
 * The input slots are used to perform and
 * track the reads of a node to an out buffer
 * of another one. The type of input slot 
 * depends on the type of the buffer.
 *
 *
 * Generic input slot:
 * contains the fields in common to each type
 * of input slot
 */
struct inslot {
    struct out_buf* src;      /* Source buffer */
};

/**
 * Linear input slot:
 * used to read from a struct l_buf
 */
struct inslot_l {
    struct out_buf* src;      /* Source buffer */
    unsigned int of_start;    /* Offset to the unread data */
};

/**
 * Circular input slot:
 * used to read from a struct c_buf
 */
#define SIZE_CACHE 128
struct inslot_c {
    struct out_buf* src;      /* Source buffer */

    size_t data_read;         /* Total data read */
    size_t of_ck;             /* Offset current chunk */

    /* Cache */
    char cache[SIZE_CACHE];   /* Circular buffer. Each read must 
                                 consume all the readable data storing
                                 it if necessary in this cache */
    unsigned int of_cdata;    /* Offset to the unread data (cache1) */
    unsigned int size_cdata;  /* Size of the unread data (cache1) */


};


struct cb_transf {
    size_t data_size;        /* Data transferred */
    size_t real_size;        /* Total size transferred */
    unsigned int cks_passed; /* Chunks finished during this trasfer */
};




/* General */
void* st_makeb(unsigned char buftype, size_t bufsize);
int st_destroyb(struct out_buf *buf);


/* Circular buffer */
inline ckcount_t cb_getckcount(struct c_buf *cb, unsigned int of_ck);
inline cksize_t cb_getcksize(struct c_buf *cb, unsigned int of_ck);
inline void cb_writechunk(struct c_buf *cb, size_t offset, const void *buf, cksize_t nbyte);
ssize_t cb_releasable (struct c_buf *cb, ckcount_t maxreads, bool blocking);
inline size_t cb_realfreespace(size_t free_space);
int cb_release(struct c_buf *cb, size_t nbyte);
int cb_acquire(struct c_buf *cb, size_t nbyte);
size_t cb_cacheread(struct inslot_c* in, void* buf, size_t nbyte);
size_t cb_dowrite(struct c_buf *cb, size_t of_start, const void *buf, size_t nbyte);
ssize_t cb_write(struct c_buf *cb, unsigned int nreaders, const void *buf, size_t nbyte);
struct cb_transf cb_read(struct c_buf *cb, size_t data_av, struct inslot_c *in, void *buf, size_t nbyte);
struct c_buf* cb_make(size_t sizebuf);
int cb_destroy(struct c_buf* b);
int isc_icc(struct inslot_c* isc, size_t of_startck, unsigned int ncks);
size_t isc_getavailable(struct inslot_c *in);
ssize_t st_cbread(struct inslot_c* in, void* buf, size_t nbyte);
struct inslot_c* st_makeinslotc(struct out_buf* b);


/* Linear buffer */
ssize_t lb_write(struct l_buf *lb, const void* buf, size_t nbyte);
int st_bufstatlb(struct l_buf* lb, int status);
ssize_t st_readlb(struct inslot_l* in, void* buf, size_t nbyte);
struct l_buf* lb_make(size_t sizebuf);
int lb_destroy(struct l_buf* b);
struct inslot_l* st_makeinslotl(struct out_buf* b);

#endif
