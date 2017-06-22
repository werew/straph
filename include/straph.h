#ifndef _TYPES_H_
#define _TYPES_H_

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "linked_fifo.h"

typedef enum{false,true} bool;
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


/**************** Node's output buffers ******************/

/* Buffer types */
#define NO_BUF   0 /* Empty slot      */
#define CIR_BUF  1 /* Circular buffer */
#define LIN_BUF  2 /* Linear buffer   */

/* Buffer status */
#define BUF_READY    0 /* Buf ready to be used:
                          just created or rewinded  */
#define BUF_ACTIVE   1 /* Buf receiving data:
                          node is executing */
#define BUF_INACTIVE 2 /* Not receiving anymore but it
                          still possible to read: node
                          has just terminated its execution */

/**
 * Out buffer container
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
 * A circular buffer provides an infinite write
 * and read capability. At every write the data
 * is incorporated inside one or more chunks. 
 * Each chunk is so composed:
 *          2 byte      2 byte      n bytes
 *      +------------+-----------+------------+
 *      | read count | size data | data ...   |
 *      +------------+-----------+------------+
 * - read count: incremented by one at each read
 * - size data : size in bytes of the data 
 *               contained in the chunk
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



#define CB_READUI16(b,o,a) { ((unsigned char*) a)[0] = b->buf[(o) % b->sizebuf];       \
                             ((unsigned char*) a)[1] = b->buf[((o)+1) % b->sizebuf];   \
                           }

#define CB_WRITEUI16(b,o,a) { b->buf[(o) % b->sizebuf] = ((unsigned char*) a)[0];      \
                              b->buf[((o)+1) % b->sizebuf] = ((unsigned char*) a)[1];  \
                            }

typedef uint16_t ckcount_t;
typedef uint16_t cksize_t ;
#define SIZE_CKHEAD (sizeof(ckcount_t)+sizeof(cksize_t))
#define MAX_CKDATASIZE 0xffff /* Max uint16_t */
#define MAX_CKSIZE  (SIZE_CKHEAD + MAX_CKDATASIZE)

/* Header of a chunk */
struct cb_ckhead{
    ckcount_t count; /* Number of reads  */
    cksize_t  size;  /* Size of the data */
};







/******************** Node's input slots *********************/


/**
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
    unsigned int cks_passed; /* Chunks finished */
};



/******************** Core structures *********************/

/* Run modes */
#define PAR_MODE 0  /* Parallel */
#define SEQ_MODE 1  /* Sequential */

/**
 * This struct is used to link each
 * node with its neighbours: the nodes
 * next to be run
 */
struct neighbour { 
    struct s_node* n;       /* Neighbour */
    unsigned char run_mode; /* Run mode of the neighbour */
};

/**
 * Node lifecycle:
 *   +----------+
 *   | INACTIVE |<---+
 *   +----+-----+    |
 *        |          |
 *     st_nup        | 
 *        |          |
 *   +----v-----+    |
 *   |  ACTIVE  |    | 
 *   +----+-----+    |
 *        |          |
 *     st_ndown      | st_nrewind
 *        |          |
 *   +----v-----+    |
 *   |TERMINATED+    |
 *   +----------+    |
 *        |          |
 *     st_join       |
 *        |          |
 *   +----v-----+    |
 *   |  JOINED  +----+
 *   +----------+
 *
 */

#define INACTIVE   0
#define ACTIVE     1
#define TERMINATED 2
#define JOINED     3
#define DOOMED     4

/**
 * Main structure representing node
 */
typedef struct s_node {

    pthread_spinlock_t launch_lock;  /* Lock used while launching 
                                        the node to avoid multiple 
                                        executions */

    void* (*entry)(struct s_node*);  /* Entry point of the module */
    unsigned int nb_parents;         /* Nb of parents liked to this node */
    unsigned int nb_startrequests;   /* Nb of times a parent tried to 
                                        launch this node */
    unsigned char status;            /* Status of this node */
    pthread_t id;                    /* Id of the module */
    void* ret;                       /* Return value */

    /* Input flow */    
    unsigned int nb_inslots;         /* Number of input slots */
    void ** inslots;                 /* Pointers to the output buffer
                                        of the source nodes when not 
                                        active. Pointers to the input
                                        slots when active */

    /* Output flow */    
    unsigned int nb_outslots;         /* Number of output buffers */
    struct out_buf* outslots;         /* Output buffers */


    /* Execution flows */
    unsigned int nb_neigh;           /* Number of neighbours */
    struct neighbour* neigh;         /* Adjacency list */


} *node;
    

/**
 * A straph is the entry point of each
 * program. A straph can be use to lauch
 * in parallel several nodes (entry points)
 */
typedef struct s_straph { 
    node* entries;           /* Entry points */
    unsigned int nb_entries; /* Number of neighbours */
} *straph;




/* Straph user's interface */
straph st_create(void);
node st_makenode(void* (*entry)(node));
int st_addnode(straph g, node n);
int st_start(straph s);
int st_join(straph s);
int st_ndestroy(node n);
int st_destroy(straph s);
int st_setbuffer(node n, unsigned int idx_buf, unsigned char buftype, size_t bufsize);
int st_nlink(node a, node b, unsigned char mode);
int st_addflow(node a, unsigned int idx_buf, node b, unsigned int islot);
int st_bufstat(node n, unsigned int slot, int status);
int st_nrewind(node n);
int st_rewind(straph st);


/* Straph's internals */
inline ckcount_t cb_getckcount(struct c_buf *cb, unsigned int of_ck);
inline cksize_t cb_getcksize(struct c_buf *cb, unsigned int of_ck);
inline void cb_writechunk(struct c_buf *cb, size_t offset, const void *buf, cksize_t nbyte);
ssize_t cb_releasable (struct c_buf *cb, ckcount_t maxreads, bool blocking);
inline size_t cb_realfreespace(size_t free_space);
int cb_release(struct c_buf *cb, size_t nbyte);
int cb_acquire(struct c_buf *cb, size_t nbyte);
size_t cb_dowrite(struct c_buf *cb, size_t of_start, const void *buf, size_t nbyte);
ssize_t cb_write(struct c_buf *cb, unsigned int nreaders, const void *buf, size_t nbyte);
size_t cb_cacheread(struct inslot_c* in, void* buf, size_t nbyte);
struct cb_transf cb_read(struct c_buf *cb, size_t data_av, struct inslot_c *in, void *buf, size_t nbyte);
int isc_icc(struct inslot_c* isc, size_t of_startck, unsigned int ncks);
size_t isc_getavailable(struct inslot_c *in);
ssize_t st_cbread(struct inslot_c* in, void* buf, size_t nbyte);
struct c_buf* cb_make(size_t sizebuf);
int cb_destroy(struct c_buf* b);
struct inslot_c* st_makeinslotc(struct out_buf* b);
ssize_t lb_write(struct l_buf *lb, const void* buf, size_t nbyte);
int st_bufstatlb(struct l_buf* lb, int status);
ssize_t st_readlb(struct inslot_l* in, void* buf, size_t nbyte);
struct l_buf* lb_make(size_t sizebuf);
int lb_destroy(struct l_buf* b);
struct inslot_l* st_makeinslotl(struct out_buf* b);
ssize_t st_read(node n, unsigned int slot, void* buf, size_t nbyte);
ssize_t st_write(node n, unsigned int slot, const void* buf, size_t nbyte);
int st_bufstat(node n, unsigned int slot, int status);
void* st_makeb(unsigned char buftype, size_t bufsize);
int st_destroyb(struct out_buf *buf);
int st_starter(struct linked_fifo *lf);
int st_nstart(node nd);
void* st_threadwrapper(void *n);
int st_nup(node nd);
void st_ndown(node nd);

#endif
