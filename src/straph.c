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








/* Results are indefined if st_setbuffer on node != INACTIVE */
/**
 * @brief Set an output buffer for the given node
 *
 *
 */
int st_setbuffer(node nd, unsigned int bufindex, 
    unsigned char buftype, size_t bufsize){

    unsigned int totbufs;      /* New total number of buffers */
    unsigned int nb_newslots;  /* Number of new slots for buffers */
    struct out_buf *bufs;      /* New array of bufs */
    void *newbuf;              /* New buffer */

    /* Extend the array if bufindex is beyond the capacity */
    if (nd->nb_outbufs <= bufindex){

        totbufs = bufindex + 1;
        bufs = realloc(nd->output_buffers, 
            totbufs * sizeof(struct out_buf));
        if (bufs == NULL) return -1;

        /* Set to NULL all the new unused structs */
        nb_newslots = totbufs - nd->nb_outbufs;
        memset(bufs + nd->nb_outbufs, 0, 
            nb_newslots * sizeof(struct out_buf));

        nd->output_buffers = bufs;
        nd->nb_outbufs = bufindex+1;
    }

    /* Create new buffer */
    newbuf = st_makeb(buftype, bufsize);
    if (newbuf == NULL) return -1;

    /* Destroy old buffer if necessary */
    if (nd->output_buffers[bufindex].buf != NULL){
        if (st_destroyb(&nd->output_buffers[bufindex]) == -1){
            free(newbuf);
            return -1;
        }
    }

    /* Update buff */
    nd->output_buffers[bufindex].type = buftype;
    nd->output_buffers[bufindex].buf  = newbuf;

    return 0;
}



/* TODO function to link nodes without IO */

int st_nlink(node a, unsigned int idx_buf, 
    node b, unsigned int islot, unsigned char mode){
    void *tmp;

    /* Check index buffer */
    if (idx_buf >= a->nb_outbufs){
        errno = EINVAL;
        return -1;
    }

    /* Add neighbour to 'a' and set the mode */
    tmp = realloc(a->neigh, (a->nb_neigh+1)*
                 sizeof(struct neighbour));
    if (tmp == NULL) return -1;
    a->neigh = tmp;
    a->neigh[a->nb_neigh].n = b;
    a->neigh[a->nb_neigh++].run_mode = mode;


    /* Add a new input slot to 'b' */
    if (b->nb_inslots <= islot){
        /* Realloc if islot is beyond the capacity */
        tmp = realloc(b->input_slots, (islot+1)*sizeof(void*));
        if (tmp == NULL){a->nb_neigh--; return -1;}

        /* Set to NULL all new slots */
        memset((void**) tmp + b->nb_inslots, 0,
                (islot+1 - b->nb_inslots)*sizeof(void*));

        b->input_slots = tmp;
        b->nb_inslots = islot+1;
    }

    b->input_slots[islot] = &a->output_buffers[idx_buf];

    return 0;
}



int st_starter(struct linked_fifo* lf){

    unsigned int i;
    node n;

    while (1){
        /* Pop and launch node */
        if ((n = lf_pop(lf)) == NULL){
            if (errno == ENOENT) break;
            return -1;
        }
        if (st_nstart(n) != INACTIVE) continue;

        /* Collect node's neighbours */
        for (i = 0; i < n->nb_neigh; i++){
            /* 
             If the running mode is parallel,
             add the node to the list 
            */
            if (n->neigh[i].run_mode != PAR_MODE) continue;
           
            if (lf_push(lf, n->neigh[i].n) == -1){
                return -1;
            }
        } 
    }
    
    return 0;
}



/* XXX what about ret in case of error ?? */
void* st_threadwrapper(void *n){
    struct linked_fifo lf;
    struct inslot_c* is;
    struct out_buf* src;
    unsigned int i;
    void *ret;
    node nd;

    nd = (node) n;

    /* Activate out buffers */
    for (i = 0; i < nd->nb_outbufs; i++){
        st_bufstat(nd, i, BUF_ACTIVE);
    }

    ret = nd->entry(n);

    /* Update status */
    nd->status = TERMINATED;

    /* Free input slots */
    for (i = 0; i < nd->nb_inslots; i++){
        is = nd->input_slots[i];
        if (is == NULL) continue;

        src = is->src;         
        if (src->type == CIR_BUF) {
            free(is->cache2);
        }
        free(is);

        nd->input_slots[i] = src;   /* Restore src */
    }

    /* Launch inactives neighbours */
    lf_init(&lf);

    /* Init list with current node's neighbours */
    for (i = 0; i < nd->nb_neigh; i++){
        if (lf_push(&lf, nd->neigh[i].n) == -1){
            lf_drop(&lf);
            return ret;
        }
    } 

    if (st_starter(&lf) == -1) lf_drop(&lf);

    /* Deactivate out buffers */
    for (i = 0; i < nd->nb_outbufs; i++){
        st_bufstat(nd,i, BUF_INACTIVE);
    }

    return ret;
}



/* returns previous status */
int st_nstart(node n){

    int err;
    unsigned int i;

    /* Lock the node */
    if ((err = pthread_spin_lock(&n->launch_lock)) != 0) {
        errno = err;
        return -1;
    }

    /* Can launch only inactives nodes */
    if (n->status != INACTIVE) {
        unsigned int status = n->status;
        pthread_spin_unlock(&n->launch_lock);
        return status;
    }

    puts("Launch");
    /* Create input slots */
    for (i = 0; i < n->nb_inslots; i++){
        void* islot;

        if (n->input_slots[i] == NULL) continue;

        switch (((struct out_buf*)n->input_slots[i])->type){
            case LIN_BUF:
                islot = st_makeinslotl((struct out_buf*) n->input_slots[i]);
                break;
            case CIR_BUF:
                islot = st_makeinslotc((struct out_buf*) n->input_slots[i]);
                break;
            default: 
                errno = EINVAL;
                pthread_spin_unlock(&n->launch_lock);
                return -1;
        }

        if (islot == NULL) {
                pthread_spin_unlock(&n->launch_lock);
                return -1;
        }

        ((struct inslot_l*) islot)->src = n->input_slots[i];
        n->input_slots[i] = islot;
    }


    /* Launch thread */
    err = pthread_create(&n->id, NULL, st_threadwrapper, n);
    if (err != 0){
        pthread_spin_unlock(&n->launch_lock);
        errno = err;
        return -1;
    }

    /* Update status and leave*/
    n->status = ACTIVE;
    if ((err = pthread_spin_unlock(&n->launch_lock)) != 0) {
        errno = err;
        return -1;
    }
    
    return INACTIVE;
}


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
        nd->status = INACTIVE;

        /* Collect neighbours */
        for (i = 0; i < nd->nb_neigh; i++){
            if (nd->neigh[i].n->status == INACTIVE) continue;
            if (lf_push(&lf, nd->neigh[i].n) == -1) goto error;
        } 
    }

    /*TODO st_rewind (set BUF_READY, and other stuffs) */

    return 0;

error:
    lf_drop(&lf);
    return -1;

}


int st_ndestroy(node n){
    int err;
    unsigned int i;

    /* Destroy out bufs */
    if (n->output_buffers != NULL){
        for (i = 0; i < n->nb_outbufs; i++){
        
            if (n->output_buffers[i].buf == NULL) continue;

            switch (n->output_buffers[i].type){
                case LIN_BUF: st_destroylb(n->output_buffers[i].buf);
                    break;
                case CIR_BUF: st_destroycb(n->output_buffers[i].buf);
                    break;
                default: errno = EINVAL;
                         return -1;  
            }
        }

        free(n->output_buffers);
    }

    free(n->input_slots);
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





