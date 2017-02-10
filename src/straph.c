#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "straph.h"

/* TODO set better naming conventions */
/* TODO improve code readablility  */



/**
 * @brief Creates a new empty straph
 * 
 * Creates a new straph which doesn't contain (yet) any node. After
 * used a straph must be freed using the function st_destroy
 *
 * @return a straph or NULL in case of error in this case errno is
 *         set appropriately
 *
 * @see st_destroy
 */
straph st_create(void){
    straph st = calloc(1, sizeof (struct s_straph));
    if (st == NULL) return NULL;

    return st;
}





/**
 * @brief Creates a new node
 *
 * Creates a new inactive node. This node can be directly used
 * in a straph by linking it to another node of the straph or
 * attaching it to the straph directly
 *
 * @param entry Entry point for the execution of the node
 * @return an inactive node or NULL in case of error, in this case
 *         errno is set appropriately
 */ 
node st_makenode(void* (*entry)(node)){
    int err;
    node nd;

    nd = calloc(1, sizeof (struct s_node));
    if (nd == NULL) return NULL;
    
    /* Initialize node */
    if ((err = pthread_spin_init(&nd->launch_lock,
                    PTHREAD_PROCESS_PRIVATE)) != 0){
        free(nd);
        errno = err;
        return NULL;
    }
    nd->entry = entry;
    nd->status = INACTIVE;

    return nd;
}





/**
 * @brief Add a node to the a straph
 *
 * Add a node to a straph. This node will be used as one of 
 * entry points of the straph when st_start is called
 *
 * @param s a straph
 * @param n a node
 * @return 0 in case of success or -1 in case of error
 *
 * @see st_start
 */
int st_addnode(straph st, node nd){
    void *new_entries;
    
    /* Extend by one the list of entries */
    new_entries = realloc(st->entries, 
        (st->nb_entries+1)*sizeof(node));

    if (new_entries == NULL) return -1;
    st->entries= new_entries;

    /* Add n to the list */
    st->entries[st->nb_entries++] = nd;

    return 0;
}





/**
 * @brief Set an output buffer for the given node
 *
 * Add or modify a buffer of a given node. Buffers are
 * used to pass data from node to node. Results are indefined 
 * if this function is used to set a buffer on a node which is
 * not inactive.
 *
 * @param nd node on which set the buffer
 * @param bufindex at which the buffer should be set. If bufindex
 *        is bigger than the actual number of buffers than all
 *        the buffers with an index between bufindex and the number
 *        of buffers will be set as NO_BUF
 * @param buftype type of the buffer, possible values are:
 *        LIN_BUF - linear buffer, provides a read/write capability
 *                  limited to bufsize. The writes who exceed the
 *                  size of the buffer are ignored. For most of the 
 *                  cases this is the recommended type when passing
 *                  data from node executed sequentially, or when the
 *                  size of the passed data is known and not too big.
 *        CIR_BUF - circular buffer, provides an unlimited read/write
 *                  capability. The speed of the writes/reads depends
 *                  on the speed of the other (e.g. a fast writer can
 *                  be slowed down by slow readers). If the reader 
 *                  has been executed in SEQ_MODE then it will acceed
 *                  only to the last portion of data written by the
 *                  writer before terminating. The size of this 
 *                  portion is undefined and depends on the size of
 *                  the buffer and the size of each write. 
 *                  TODO implement this last part
 *        NO_BUF  - no buffer will be set, every buffer previously 
 *                  set at bufindex will be eliminated 
 * @param bufsize size of the buffer. A size of zero has the same
 *        effect as NO_BUF. When NO_BUF is given as buftype this
 *        parameter is ignored.
 * @return 0 in case of success, -1 otherwise. This function sets
 *         errno.
 */
int st_setbuffer(node nd, unsigned int bufindex, 
    unsigned char buftype, size_t bufsize){

    unsigned int totbufs;      /* New total number of buffers */
    unsigned int nb_newslots;  /* Number of new slots for buffers */
    struct out_buf *bufs;      /* New array of bufs */
    void *newbuf;              /* New buffer */

    /* Extend the array if bufindex is beyond the actual capacity */
    if (nd->nb_outslots <= bufindex){

        totbufs = bufindex + 1;
        bufs = realloc(nd->outslots, 
            totbufs * sizeof(struct out_buf));
        if (bufs == NULL) return -1;

        /* 
          Set all the new unused structs (slots) with:
          buf = NULL and size = 0.
          Those buffers are considered as having type  NO_BUF 
        */
        nb_newslots = totbufs - nd->nb_outslots;
        memset(bufs + nd->nb_outslots, 0,
            nb_newslots * sizeof(struct out_buf));

        nd->outslots = bufs;
        nd->nb_outslots = bufindex+1;
    }

    /* Create new buffer */
    if (buftype != NO_BUF && bufsize > 0){
        newbuf = st_makeb(buftype, bufsize);
        if (newbuf == NULL) return -1;
    } else {
        newbuf = NULL;
    }

    /* Destroy old buffer if necessary */
    if (nd->outslots[bufindex].buf != NULL){
        if (st_destroyb(&nd->outslots[bufindex]) == -1){
            free(newbuf);
            return -1;
        }
    }

    /* Update buff */
    nd->outslots[bufindex].type = buftype;
    nd->outslots[bufindex].buf  = newbuf;

    return 0;
}





/* TODO function to link nodes without IO */
/**
 * @brief Creates an execution-edge between two nodes
 *
 * Links two nodes creating an execution-edge. 
 * Execution-edges are directed edges between two nodes
 * and define the order of the execution of each node.
 * Nodes are executed following the topological order
 * defined by the execution-edges.
 *
 * @param a node source of the execution-edge
 * @param b node destination of the execution-edge
 * @param mode mode of execution. Available options are:
 *        SEQ_MODE: wait for the termination of node a before
 *                  launching node b
 *        PAR_MODE: don't wait for node a to terminate, execute
 *                  node b in parallel
 * @return 0 in case of success or -1 otherwise, in this
 *         case errno is set
 */
int st_nlink(node a, node b, unsigned char mode){
    void *new_neigh;

    /* Add neighbour to 'a' and set the mode */
    new_neigh = realloc(a->neigh, (a->nb_neigh+1)*
        sizeof(struct neighbour));
    if (new_neigh == NULL) return -1;

    a->neigh = new_neigh;
    a->neigh[a->nb_neigh].n = b;
    a->neigh[a->nb_neigh++].run_mode = mode;
    b->nb_parents++;

    return 0;
}





/**
 * @brief Add an io-edge between two nodes
 * 
 * Adds an io-edge between two nodes. Io-edges represent
 * data flows between nodes. 
 *
 * @param a writer: node source of the io-edge
 * @param outslot number of the outslot of a
 * @param b reader: node destination of the io-edge 
 * @param inslot number of the receiving inslot of b
 * @return 0 in case of success or -1 otherwise, in this
 *         case errno is set
 */
int st_addflow(node a, unsigned int outslot,
               node b, unsigned int inslot ){

    /* Check index buffer */
    if (outslot >= a->nb_outslots){
        errno = EINVAL;
        return -1;
    }

    /* 
     TODO: 
        - increment the count on the buffer
        - decrement the count of the previous buffer
          if the slot was already used
    */

    /* Add a new input slot to 'b' */
    if (b->nb_inslots <= inslot){

        /* Realloc if inslot is beyond the capacity */
        void *tmp = realloc(b->inslots, (inslot+1)*sizeof(void*));
        if (tmp == NULL){a->nb_neigh--; return -1;}

        /* Set to NULL all new slots */
        memset((void**) tmp + b->nb_inslots, 0,
                (inslot+1 - b->nb_inslots)*sizeof(void*));

        b->inslots = tmp;
        b->nb_inslots = inslot+1;
    }

    b->inslots[inslot] = &a->outslots[outslot];

    return 0;
}





/**
 * @brief Launches all the nodes of an initialized fifo and
 *        their children
 * 
 * Launches all the nodes inside the linked list given as argument.
 * Then launches all the children nodes reachable trough execution edges 
 * with run_mode == PAR_MODE
 *
 * @param lf a pointer to a struct linked_fifo initialized with
 *        the nodes to launch
 * @return 0 in case of success or -1 otherwise, in this
 *         case errno is set
 */
int st_starter(struct linked_fifo *lf){

    node nd;
    unsigned int i;

    while (1){
        /* Pop next node */
        if ((nd = lf_pop(lf)) == NULL){
            if (errno == ENOENT) break;
            return -1;
        }

        /* Launch node */ 
        switch (st_nstart(nd)){
            case  0: continue ; /* Not launched */
            case -1: return -1; /* Error        */
        }

        /* Collect node's neighbours */
        for (i = 0; i < nd->nb_neigh; i++){
            /* 
             If the running mode is parallel,
             add the node to the list 
            */
            if (nd->neigh[i].run_mode != PAR_MODE) continue;
           
            if (lf_push(lf, nd->neigh[i].n) == -1){
                return -1;
            }
        } 
    }
    
    return 0;
}

/**
 * @brief bring down a node to the status terminated
 *
 * This function shall be called on a node after it's 
 * routine has terminated to change it's status to 
 * TERMINATED and free the data structures not longer
 * needed.
 *
 * @param nd the node to bring down
 */
void st_ndown(node nd){

    unsigned int i;

    /* Update status */
    nd->status = TERMINATED;

    /* Free input slots */
    for (i = 0; i < nd->nb_inslots; i++){

        struct inslot_c *inslot;
        struct out_buf *src;

        inslot = nd->inslots[i];
        if (inslot == NULL) continue;

        src = inslot->src;         
        if (src->type == CIR_BUF) {
            free(inslot->cache2);
        }
        free(inslot);

        nd->inslots[i] = src;   /* Restore src */
    }

    /* Deactivate out buffers */
    for (i = 0; i < nd->nb_outslots; i++){
        st_bufstat(nd,i, BUF_INACTIVE);
    }
}





/* XXX what about ret in case of error ?? */
/**
 * @brief Activates, run and deactivates a node
 * 
 *
 */
void* st_threadwrapper(void *n){
    node nd;
    void *ret;
    unsigned int i;
    struct linked_fifo lf;

    nd = (node) n;

    /**** Initialization *****/
    /* Activate out buffers */
    for (i = 0; i < nd->nb_outslots; i++){
        st_bufstat(nd, i, BUF_ACTIVE);
    }


    /**** Execution *****/
    ret = nd->entry(n);


    /**** Prologue *****/
    st_ndown(nd);

    /* Re-run starter from the neighbours having SEQ_MODE*/
    lf_init(&lf);
    for (i = 0; i < nd->nb_neigh; i++){
        if (nd->neigh[i].run_mode != SEQ_MODE) continue;
        if (lf_push(&lf, nd->neigh[i].n) == -1){
            lf_drop(&lf);
            return ret;
        }
    } 
    if (st_starter(&lf) == -1) lf_drop(&lf);


    return ret;
}




/**
 * 
 */
int st_nup(node nd){
    int err;
    unsigned int i;

    /* Create input slots */
    for (i = 0; i < nd->nb_inslots; i++){
        void* islot;

        if (nd->inslots[i] == NULL) continue;

        switch (((struct out_buf*)nd->inslots[i])->type){
            case LIN_BUF:
                islot = st_makeinslotl((struct out_buf*) nd->inslots[i]);
                break;
            case CIR_BUF:
                islot = st_makeinslotc((struct out_buf*) nd->inslots[i]);
                break;
            default: 
                errno = EINVAL;
                return -1;
        }

        if (islot == NULL) return -1;

        ((struct inslot_l*) islot)->src = nd->inslots[i];
        nd->inslots[i] = islot;
    }

    /* Update status */
    nd->status = ACTIVE;

    /* Launch thread */
    err = pthread_create(&nd->id, NULL, st_threadwrapper, nd);
    if (err != 0){
        errno = err;
        return -1;
    }

    return 0;
}





/**
 * @brief send a start request to an inactive node
 *
 * Try to launch an inactive node by sending a start request. 
 * If the number of start requests is equal or greater than
 * the number of its parents, the node will be launched.
 *
 * @param nd node to which send a start request
 * @return -1 in case of error, otherwise 1 if the
 *          node has been launched or 0 if the node
 *          is not ready (not enough start requests)
 *          to be launched
 */
int st_nstart(node nd){

    int err, ret;
    /* Lock the node */
    if ((err = pthread_spin_lock(&nd->launch_lock)) != 0) {
        errno = err;
        return -1;
    }

    /* Can launch only inactives nodes */
    if (nd->status != INACTIVE ) {
        errno = EINVAL;
        pthread_spin_unlock(&nd->launch_lock);
        return -1;
    }
        
    /* Add start request */
    nd->nb_startrequests += 1; 
    if (nd->nb_startrequests < nd->nb_parents){
        /* The node needs to wait for other parents */
        ret = 0;

    } else {
        /* The node is ready to be launched */

        /* Bring node up */
        if (st_nup(nd) == -1){
            pthread_spin_unlock(&nd->launch_lock);
            return -1;
        }

        ret = 1;
    }

    /* Leave */
    if ((err = pthread_spin_unlock(&nd->launch_lock)) != 0) {
        errno = err;
        return -1;
    }

    return ret;
}




/**
 * @brief join all the threads of a straph
 *
 * This function will wait the termination of all 
 * node's threads and join them. The value retrieved 
 * by each thread is stored inside the respective node.
 * After joined the straph is rewinded and every node's
 * status is brought back from TERMINATED to INACTIVE 
 *
 * @param st running straph to join
 * @return 0 in case of success or -1 otherwise, in this
 *         case errno is set
 */
int st_join(straph st){
    node nd;
    int err;
    unsigned int i;
    struct linked_fifo lf;


    lf_init(&lf);

    /* Init fifo with straph's entries */
    for (i = 0; i < st->nb_entries; i++){
        if (lf_push(&lf, st->entries[i]) == -1) goto error;
    }

    /* Join nodes */
    nd = NULL; 
    while (1){

        /* Pop and join node */
        nd = lf_pop(&lf);
        if (nd == NULL){
            if (errno == ENOENT) break;
            goto error;
        }

        err = pthread_join(nd->id, &nd->ret);
        if (err != 0){
            errno = err;
            goto error;
        }

        /* Once joined return inactive */
        nd->status = INACTIVE; /* TODO do it in st_rewind */

        /* Collect neighbours */
        for (i = 0; i < nd->nb_neigh; i++){
            if (nd->neigh[i].n->status == INACTIVE) continue;
            if (lf_push(&lf, nd->neigh[i].n) == -1) goto error;
        } 
    }

    /*TODO st_rewind (set BUF_READY, status INACTIVE,
      nb_startrequests, and other stuffs) */

    return 0;

error:
    lf_drop(&lf);
    return -1;
}


int st_ndestroy(node n){
    int err;
    unsigned int i;

    /* Destroy out bufs */
    if (n->outslots != NULL){
        for (i = 0; i < n->nb_outslots; i++){
        
            if (n->outslots[i].buf == NULL) continue;

            switch (n->outslots[i].type){
                case LIN_BUF: st_destroylb(n->outslots[i].buf);
                    break;
                case CIR_BUF: st_destroycb(n->outslots[i].buf);
                    break;
                default: errno = EINVAL;
                         return -1;  
            }
        }

        free(n->outslots);
    }

    free(n->inslots);
    free(n->neigh);

    err = pthread_spin_destroy(&n->launch_lock);
    if (err != 0){
        errno = err;
        return -1;
    }

    free(n);

    return 0;
}


/* TODO */
int st_destroy(straph s){
    unsigned int i;
    node n = NULL; 
    struct linked_fifo lf1,lf2;
    lf_init(&lf1);
    lf_init(&lf2);


    /* 
     Collect nodes to avoid double frees (collected 
     nodes are marked with status = DOOMED) 
    */

    
    for (i = 0; i < s->nb_entries; i++){
        if (lf_push(&lf1, s->entries[i]) == -1) goto error;
    }
    while (1){

        n = lf_pop(&lf1);
        if (n == NULL){
            if (errno == ENOENT) break;
            goto error;
        }

        /* Skip if already DOOMED  */ 
        if (n->status == DOOMED) continue;

        /* Set doomed if not doomed */
        if (lf_push(&lf2, n) == -1) goto error;
        n->status = DOOMED;
        
        /* Collect not neighbours */
        for (i = 0; i < n->nb_neigh; i++){
            if (lf_push(&lf1, n->neigh[i].n) == -1) goto error;
        } 
    }

    /* Destroy collected nodes */
    while ((n = lf_pop(&lf2))){
        if (st_ndestroy(n) == -1) goto error;
    }
    if (errno != ENOENT) goto error;

    /* Free straph */
    free(s->entries);
    free(s);
    
    return 0;

error:
    lf_drop(&lf1);
    lf_drop(&lf2);
    return -1;
}


int st_start(straph s){

    unsigned int i;
    struct linked_fifo lf;
    lf_init(&lf);

    /* Init fifo with straph's entries */
    for (i = 0; i < s->nb_entries; i++){
        if (lf_push(&lf, s->entries[i]) == -1){
            lf_drop(&lf);
            return -1;
        }
    }

    if (st_starter(&lf) == -1){
        lf_drop(&lf);
        return -1;
    }

    return 0;
}





