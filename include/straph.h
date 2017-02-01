#ifndef _TYPES_H_
#define _TYPES_H_

#include <pthread.h>



/**************** Useful datatypes  ******************/

/* Linked fifo's cell */
struct lf_cell {
    void* element;         // Content of the cell
    struct lf_cell* next;  // Next linked cell
};

/* Linked fifo */
struct linked_fifo {
    struct lf_cell* first; // Least recent cell of the list
    struct lf_cell* last;  // Most recent cell of the list
};

/* Spinlock-based readers-writer lock */
typedef struct rw_slock {
    unsigned int n_r;      // Readers count
    pthread_spinlock_t r;  // Lock for readers count
    pthread_spinlock_t w;  // Writer lock
} rw_spinlock;


/**************** Node's output buffers ******************/

/* Buffer types */
#define CIR_BUF  0 
#define LIN_BUF  1

/**
 * Out buffer container
 */
struct out_buf {
    unsigned char type;      // Type of the buffer 
    void* buf;               // Output buffer
};

/**
 * Linear buffer:
 * A linear buffer provides a finite write and
 * read capability (max sizebuf). The data written
 * out of the capacity of the buffer shall be ignored.
 */
struct l_buf {
    char* buf;             // Pointer to the buf 
    unsigned int sizebuf;  // Size of the buf
    unsigned int of_empty; // Offset to the unwritten zone
    rw_spinlock lock;      // Readers-writer lock to regulate of_empty access
};


/**
 * Circular buffer:
 * A circular buffer provides an infinite write
 * and read capability. At every write the data
 * is incorporated inside one or more chunks. 
 * Each chunk is so composed:
 *          1 byte      1 byte      n bytes
 *      +------------+-----------+------------+
 *      | read count | size data | data ...   |
 *      +------------+-----------+------------+
 * - read count: incremented by one at each read
 * - size data : size in bytes of the data 
 *               contained in the chunk
 */
struct c_buf {
    char* buf;                  // Pointer to the buf
    unsigned int sizebuf;       // Size of the buf
    unsigned int of_firstck;    // Offset to the first used chunk
    unsigned int of_lastck;     // Offset to the last used chunk

    pthread_mutex_t access_lock;      // Mutex for buffer access
    pthread_mutex_t nb_readers_mutex; // Mutex for read access
    unsigned int nb_readers;          // Number of readers actives
};





/******************** Node's input slots *********************/

/**
 * The input slots are used to perform and
 * track the reads of a node to an out buffer
 * of another one. The type of input slot 
 * depends on the type of the buffer.
 *
 *
 *
 * Linear input slot:
 * used to read from a struct l_buf
 */
struct inslot_l {
    struct out_buf* src;      // Source buffer
    unsigned int of_start;    // Offset to the unread data
};

/**
 * Circular input slot:
 * used to read from a struct c_buf
 */
#define SIZE_CACHE1 128
struct inslot_c {
    struct out_buf* src;      // Source buffer

    char cache1[SIZE_CACHE1]; // Circular buffer. Each read must consume
                              // all the readable data storing it if necessary
                              // in this cache
    unsigned int of_start;    // Offset to the unread data (cache1)
    unsigned int of_end;      // Offset to the end of the unread data (cache1)

    char* cache2;             // Dynamic size cache to use only when cache1 is full
    unsigned int of_start2;   // Offset to the unread data
    unsigned int size_cache2; // Size of the cache2

};





/******************** Core structures *********************/

/* Run modes */
#define PAR_MODE 0  // Parallel
#define SEQ_MODE 1  // Sequential

/**
 * This struct is used to link each
 * node with its neighbours: the nodes
 * next to be run
 */
struct neighbour { 
    struct s_node* n;       // Neighbour
    unsigned char run_mode; // Run mode of the neighbour
};

/**
 * Node lifecycle:
 *   +----------+
 *   | INACTIVE |<---+
 *   +----+-----+    |
 *        |          |
 *   launch_straph   | 
 *        |          |
 *   +----v-----+    |
 *   |  ACTIVE  |    | join_straph
 *   +----+-----+    |
 *        |          |
 *  thread returns   |
 *        |          |
 *   +----v-----+    |
 *   |TERMINATED+----+
 *   +----------+
 *
 */
#define INACTIVE   0
#define ACTIVE     1
#define TERMINATED 2
#define DOOMED     3

/**
 * Main structure representing node
 */
typedef struct s_node {

    pthread_spinlock_t launch_lock;  // Lock used while launching the node
                                     // to avoid multiple executions

    void* (*entry)(struct s_node*);  // Entry point of the module
    unsigned char status;            // Status of this node
    pthread_t id;                    // Id of the module
    void* ret;                       // Return value

    /* Input flow */    
    unsigned int nb_inslots;         // Number of input slots
    void ** input_slots;             // Pointers to the output buffers
                                     // of the source nodes when not active.
                                     // Pointers to the input slots
                                     // when active

    /* Output flow */    
    unsigned int nb_outbufs;         // Number of output buffers
    struct out_buf* output_buffers;  // Output buffers


    /* Edges */
    struct neighbour* neigh;         // Adjacency list
    unsigned int nb_neigh;           // Number of neighbours


} *node;
    

/**
 * A straph is the entry point of each
 * program. A straph can be use to lauch
 * in parallel several nodes (entry points)
 */
typedef struct s_straph { 
    node* entries;           // Entry points
    unsigned int nb_entries; // Number of neighbours
} *straph;









straph new_straph(void);
node new_node(void* (*entry)(node));
int add_start_node(straph g, node n);
struct l_buf* new_lbuf(size_t sizebuf);
struct c_buf* new_cbuf(size_t sizebuf);
void lf_init(struct linked_fifo* lf);
int lf_push(struct linked_fifo* lf, void* el);
void* lf_pop(struct linked_fifo* lf);
void lf_drop(struct linked_fifo* lf);
void* routine_wrapper(void* n);
int launch_node(node n);
int launch_straph(straph s);
struct inslot_l* new_inslot_l(struct out_buf* b);
struct inslot_l* new_inslot_c(struct out_buf* b);
int join_straph(straph s);
int node_destroy(node n);
int cbuf_destroy(struct c_buf* b);
int lbuf_destroy(struct l_buf* b);
int set_buffer(node n, unsigned int idx_buf, 
               unsigned char buftype, size_t bufsize);
int link_nodes(node a, unsigned int idx_buf, 
               node b, unsigned int islot, unsigned char mode);

int rw_spinlock_rlock(rw_spinlock l);
int rw_spinlock_runlock(rw_spinlock l);
int rw_spinlock_wlock(rw_spinlock l);
int rw_spinlock_wunlock(rw_spinlock l);
int rw_spinlock_init(rw_spinlock* l);
int rw_spinlock_destroy(rw_spinlock l);

#endif
