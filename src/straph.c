#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "straph.h"

// TODO set better naming conventions
// TODO improve code readablility 

straph st_create(void){
    straph s = calloc(1, sizeof (struct s_straph));
    if (s == NULL) return NULL;

    return s;
}


/**
 * Creates a new node. At the beginning each
 * node is inactive.
 * @param entry Entry point for the execution of the node
 * @param bufsize Size of the output buffer for this node.
 *        If bufsize is 0 then the output of this node will
 *        be ignored
 * @return A new inactive node, or NULL in case of error
 * @note When a node is linked to another using the SEQ_MODE
 *       running mode, then sizebuf represent the max size
 *       of the output of the first node
 */ 
node st_makenode(void* (*entry)(node)){
    node n = calloc(1, sizeof (struct s_node));
    if (n == NULL) return NULL;
    
    // Init lock
    int err;
    if ((err = pthread_spin_init(&n->launch_lock,
                    PTHREAD_PROCESS_PRIVATE)) != 0){
        free(n);
        errno = err;
        return NULL;
    }

    // Set entry point
    n->entry = entry;

    return n;
}



int st_addnode(straph g, node n){
    void* tmp = realloc(g->entries, 
                (g->nb_entries+1)*sizeof(node));
    if (tmp == NULL) return -1;

    g->entries= tmp;
    g->entries[g->nb_entries++] = n;

    return 0;
}








/* Results are indefined if st_setbuffer on node != INACTIVE */
int st_setbuffer(node n, unsigned int idx_buf, 
    unsigned char buftype, size_t bufsize){

    /* Increase array size if necessary */
    if (n->nb_outbufs <= idx_buf){
        // Realloc if idx_buf is beyond the capacity
        struct out_buf* tmp = realloc(n->output_buffers, 
                    (idx_buf+1)*sizeof(struct out_buf));
        if (tmp == NULL) return -1;

        // Set to NULL all new slots
        memset(tmp + n->nb_outbufs, 0,(idx_buf+1-n->nb_outbufs)*
                sizeof(struct out_buf));

        n->output_buffers = tmp;
        n->nb_outbufs = idx_buf+1;
    }

    /* Create new buffer */
    void* newbuf;
    switch (buftype){
        case CIR_BUF: newbuf = st_makecb(bufsize);
            break;
        case LIN_BUF: newbuf = st_makelb(bufsize);
            break;
        default: errno = EINVAL;
                 return -1;
    }
    if (newbuf == NULL) return -1;


    /* Free old buffer */
    if (n->output_buffers[idx_buf].buf != NULL){
        switch (n->output_buffers[idx_buf].type){
            case LIN_BUF: st_destroylb(n->output_buffers[idx_buf].buf);
                break;
            case CIR_BUF: st_destroycb(n->output_buffers[idx_buf].buf);
                break;
            default: free(newbuf);
                     errno = EINVAL;
                     return -1;  
        }
    }

    /* Update buff */
    n->output_buffers[idx_buf].type = buftype;
    n->output_buffers[idx_buf].buf  = newbuf;

    return 0;
}



// TODO function to link nodes without IO

int st_nlink(node a, unsigned int idx_buf, 
    node b, unsigned int islot, unsigned char mode){

    // Check index buffer
    if (idx_buf >= a->nb_outbufs){
        errno = EINVAL;
        return -1;
    }

    // Add neighbour to 'a' and set the mode
    void* tmp = realloc(a->neigh, (a->nb_neigh+1)*
                 sizeof(struct neighbour));
    if (tmp == NULL) return -1;
    a->neigh = tmp;
    a->neigh[a->nb_neigh].n = b;
    a->neigh[a->nb_neigh++].run_mode = mode;


    // Add a new input slot to 'b'
    if (b->nb_inslots <= islot){
        // Realloc if islot is beyond the capacity
        tmp = realloc(b->input_slots, (islot+1)*sizeof(void*));
        if (tmp == NULL){a->nb_neigh--; return -1;}

        // Set to NULL all new slots
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
            // If the running mode is parallel,
            // add the node to the list
            if (n->neigh[i].run_mode != PAR_MODE) continue;
           
            if (lf_push(lf, n->neigh[i].n) == -1){
                return -1;
            }
        } 
    }
    
    return 0;
}



// XXX what about ret in case of error ??
void* st_threadwrapper(void* n){

    node nd = (node) n;

    /* Activate out buffers */
    unsigned int i;
    for (i = 0; i < nd->nb_outbufs; i++){
        st_bufstat(nd, i, BUF_ACTIVE);
    }

    void* ret = nd->entry(n);

    /* Update status */
    nd->status = TERMINATED;

    /* Free input slots */
    for (i = 0; i < nd->nb_inslots; i++){
        struct inslot_c* is = nd->input_slots[i];
        if (is == NULL) continue;

        struct out_buf* src = is->src;         
        if (src->type == CIR_BUF) {
            free(is->cache2);
        }
        free(is);

        nd->input_slots[i] = src;   // Restore src
    }

    /* Launch inactives neighbours */
    struct linked_fifo lf;
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

    /* Lock the node */
    int err;
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
    unsigned int i;
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


int st_join(straph s){
     
    struct linked_fifo lf;
    lf_init(&lf);

    /* Init fifo with straph's entries */
    unsigned int i;
    for (i = 0; i < s->nb_entries; i++){
        if (lf_push(&lf, s->entries[i]) == -1) goto error;
    }

    /* Join nodes */
    node n = NULL; 
    while (1){

        /* Pop and join node */
        n = lf_pop(&lf);
        if (n == NULL){
            if (errno == ENOENT) break;
            goto error;
        }

        int err = pthread_join(n->id, &n->ret);
        if (err != 0){
            errno = err;
            goto error;
        }

        /* Once joined return inactive */
        n->status = INACTIVE;

        /* Collect neighbours */
        for (i = 0; i < n->nb_neigh; i++){
            if (n->neigh[i].n->status == INACTIVE) continue;
            if (lf_push(&lf, n->neigh[i].n) == -1) goto error;
        } 
    }

    //TODO st_rewind (set BUF_READY, and other stuffs)

    return 0;

error:
    lf_drop(&lf);
    return -1;

}


int st_ndestroy(node n){
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

    int err = pthread_spin_destroy(&n->launch_lock);
    if (err != 0){
        errno = err;
        return -1;
    }

    free(n);

    return 0;
}


// TODO
int st_destroy(straph s){
    unsigned int i;
    node n = NULL; 
    struct linked_fifo lf1,lf2;
    lf_init(&lf1);
    lf_init(&lf2);


    /* Collect nodes to avoid double frees (collected 
       nodes are marked with status = DOOMED) */

    
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

    struct linked_fifo lf;
    lf_init(&lf);

    /* Init fifo with straph's entries */
    unsigned int i;
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





