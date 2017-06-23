#ifndef _STRAPH_H_
#define _STRAPH_H_

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "linked_fifo.h"
#include "common.h"


/* Buffer types */
#define NO_BUF   0 /* Empty slot      */
#define CIR_BUF  1 /* Circular buffer */
#define LIN_BUF  2 /* Linear buffer   */

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


void* st_threadwrapper(void *n);
int st_starter(struct linked_fifo *lf);
int st_nstart(node nd);
int st_nup(node nd);
void st_ndown(node nd);


straph st_create(void);
node st_makenode(void* (*entry)(node));
int st_addnode(straph g, node n);
int st_start(straph s);
int st_join(straph s);
int st_ndestroy(node n);
int st_destroy(straph s);
int st_setbuffer(node n, unsigned int idx_buf, unsigned char buftype, size_t bufsize);
int st_nlink(node a, node b, unsigned char mode);
int st_addflow(node a, unsigned int idx_buf, node b, unsigned int is);
int st_nrewind(node n);
int st_rewind(straph s);
ssize_t st_read(node n, unsigned int slot, void* buf, size_t nbyte);
ssize_t st_write(node n, unsigned int slot, const void* buf, size_t nbyte);
int st_bufstat(node n, unsigned int slot, int status);






#endif
