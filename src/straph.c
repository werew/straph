#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "straph.h"

// TODO set better naming conventions
// TODO improve code readablility 

straph new_straph(void){
    straph s = calloc(1, sizeof (struct s_straph));
    if (s == NULL) return NULL;

    return s;
}

ssize_t write_lb(struct l_buf* lb, const void* buf, size_t nbyte){

    // Calculate max write capability 
    // Note that the writer has full read-access to 
    // of_size because it's the only potential writer
    size_t space_available = lb->sizebuf-lb->of_empty;
    size_t write_size = (space_available < nbyte)? 
                         space_available : nbyte;

    if (write_size == 0) return nbyte;

    printf("Writing: %ld bytes\n", write_size);

    memcpy(&lb->buf[lb->of_empty], buf, write_size);
    
    // Update offset
    int err;
    if ((err = pthread_mutex_lock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    lb->of_empty += write_size; // Update

    if ((err = pthread_mutex_unlock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    // Signal new available data
    if ((err = pthread_cond_broadcast(&lb->cond)) != 0) {
        errno = err;
        return -1;
    }
    
    return nbyte; 
}

int setbufstat_lb(struct l_buf* lb, int status){
    // Update offset
    int err;
    if ((err = pthread_mutex_lock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    lb->status = status; // Update

    if ((err = pthread_mutex_unlock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    // Awake every waiting reader 
    if ((err = pthread_cond_broadcast(&lb->cond)) != 0) {
        errno = err;
        return -1;
    }
    
    return 0; 
}

int setbufstat(node n, unsigned int slot, int status){

    if (n->nb_outbufs <= slot               ||
        n->output_buffers[slot].buf == NULL ){
        errno = ENOENT;
        return -1;
    }

    switch (n->output_buffers[slot].type){
        case LIN_BUF: 
            return setbufstat_lb(n->output_buffers[slot].buf, status);
        case CIR_BUF: 
            return 0; 
        default: 
            errno = EINVAL;
            return -1;
    }
}




ssize_t read_lb(struct inslot_l* in, void* buf, size_t nbyte){

    // Source buffer
    struct l_buf* lb = in->src->buf;
    
    // Read the minimum between the requested size and
    // the max size of the remaining buffer
    size_t max_read = lb->sizebuf - in->of_start;
    nbyte = (nbyte < max_read)? nbyte : max_read;

    // Ignore reads of zero bytes
    if (nbyte == 0) return 0;

    // Lock access     
    int err;
    if ((err = pthread_mutex_lock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }
    
    // Wait condition
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

    // Unlock access
    if ((err = pthread_mutex_unlock(&lb->mutex)) != 0) {
        errno = err;
        return -1;
    }

    // Perform read
    memcpy(buf, &lb->buf[in->of_start], nbyte);
    in->of_start += nbyte;

    return nbyte; 
}



ssize_t st_read(node n, unsigned int slot, void* buf, size_t nbyte){

    if (n->nb_inslots <= slot        ||
        n->input_slots[slot] == NULL ){
        return 0;
    }

    // Get out buffer
    struct inslot_l* islot = n->input_slots[slot];
    struct out_buf* ob = islot->src; 

    if (ob == NULL) return 0;

    switch (ob->type){
        case LIN_BUF: 
            return read_lb(n->input_slots[slot], buf, nbyte);
        case CIR_BUF: 
            return 0; 
        default: 
            errno = EINVAL;
            return -1;
    }
}

ssize_t st_write(node n, unsigned int slot, 
              const void* buf, size_t nbyte){

    // XXX should a node detect NULL buffers ?

    // If no slot is available (user choice) 
    // act as the write was successful 
    if (n->nb_outbufs <= slot               ||
        n->output_buffers[slot].buf == NULL ){
        return nbyte;
    }

    switch (n->output_buffers[slot].type){
        case LIN_BUF: 
            return write_lb(n->output_buffers[slot].buf,
                            buf, nbyte);
        case CIR_BUF: 
            return 0; //write_cb(n->output_buffers[slot].buf,
                       //     buf, nbyte);
        default: 
            errno = EINVAL;
            return -1;
    }
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
node new_node(void* (*entry)(node)){
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



int add_start_node(straph g, node n){
    void* tmp = realloc(g->entries, 
                (g->nb_entries+1)*sizeof(node));
    if (tmp == NULL) return -1;

    g->entries= tmp;
    g->entries[g->nb_entries++] = n;

    return 0;
}

struct l_buf* new_lbuf(size_t sizebuf){
    struct l_buf* b = malloc(sizeof(struct l_buf));
    if (b == NULL) return NULL;

    b->buf = malloc(sizebuf);
    if (b->buf == NULL) {
        free(b);
        return NULL;
    }

    b->sizebuf = sizebuf;
    b->of_empty = 0;
    b->status = BUF_READY;

    int err;
    if ((err = pthread_mutex_init(&b->mutex, NULL)) != 0 ||
        (err = pthread_cond_init(&b->cond, NULL))   != 0 ){
        free(b->buf);
        free(b);
        return NULL;
    }

    return b;
}



struct c_buf* new_cbuf(size_t sizebuf){
    struct c_buf* b = malloc(sizeof(struct c_buf));
    if (b == NULL) return NULL;

    int err;
    if ((err = pthread_mutex_init(&b->access_lock,
                    NULL)) != 0){
        free(b);
        errno = err;
        return NULL;
    }

    if ((err = pthread_mutex_init(&b->nb_readers_mutex,
                    NULL)) != 0){
        pthread_mutex_destroy(&b->access_lock);
        free(b);
        errno = err;
        return NULL;
    }

    b->buf = malloc(sizebuf);
    if (b->buf == NULL) {
        err = errno;
        pthread_mutex_destroy(&b->access_lock);
        pthread_mutex_destroy(&b->nb_readers_mutex);
        free(b);
        errno = err;
        return NULL;
    }

    b->sizebuf = sizebuf;
    b->of_firstck = 0;
    b->of_lastck  = 0;
    b->nb_readers = 0;

    return b;
}

/* Results are indefined if set_buffer on node != INACTIVE */
int set_buffer(node n, unsigned int idx_buf, 
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
        case CIR_BUF: newbuf = new_cbuf(bufsize);
            break;
        case LIN_BUF: newbuf = new_lbuf(bufsize);
            break;
        default: errno = EINVAL;
                 return -1;
    }
    if (newbuf == NULL) return -1;


    /* Free old buffer */
    if (n->output_buffers[idx_buf].buf != NULL){
        switch (n->output_buffers[idx_buf].type){
            case LIN_BUF: lbuf_destroy(n->output_buffers[idx_buf].buf);
                break;
            case CIR_BUF: cbuf_destroy(n->output_buffers[idx_buf].buf);
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

int link_nodes(node a, unsigned int idx_buf, 
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







void lf_init(struct linked_fifo* lf){
    lf->first = NULL;
    lf->last= NULL;
}
 

/**
 * Adds a new element at the top of the list
 * @param lf A pointer to a linked fifo
 * @param el A pointer to the element to add
 * @return 0 in case of success, -1 otherwise
 */ 
int lf_push(struct linked_fifo* lf, void* el){
    struct lf_cell* new = malloc(sizeof(struct lf_cell));
    if (new == NULL) return -1;

    new->element = el;
    new->next = NULL;

    if (lf->first == NULL) lf->first = new;
    if (lf->last  != NULL) lf->last->next = new;

    lf->last = new;

    return 0;
}

/**
 * Pop an element from the end of a list
 * @param lf A pointer to a linked fifo
 * @return The last element of the list, of NULL in case of error
 */
void* lf_pop(struct linked_fifo* lf){
    struct lf_cell* f = lf->first;
    if (f == NULL){
        errno = ENOENT;
        return NULL;
    }

    lf->first = f->next;
    if (lf->last == f) lf->last = f->next;

    void* el = f->element;
    free(f);

    return el;
}





void lf_drop(struct linked_fifo* lf){

    struct lf_cell *c1,*c2;
    
    c1 = lf->first;

    while (c1 != NULL){
        c2 = c1->next;
        free(c1);
        c1 = c2;
    }
   
    lf_init(lf);
}

struct inslot_l* new_inslot_l(struct out_buf* b){
    struct inslot_l* is = malloc(sizeof(struct inslot_l));
    if (is == NULL) return NULL;

    is->src = b;
    is->of_start = 0;
    return is;
}

struct inslot_l* new_inslot_c(struct out_buf* b){
    struct inslot_l* is = calloc(1,sizeof(struct inslot_l));
    if (is == NULL) return NULL;

    is->src = b;
    return is;
}

int launcher(struct linked_fifo* lf){

    unsigned int i;
    node n;

    while (1){
        /* Pop and launch node */
        if ((n = lf_pop(lf)) == NULL){
            if (errno == ENOENT) break;
            return -1;
        }
        if (launch_node(n) != INACTIVE) continue;

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
void* routine_wrapper(void* n){

    node nd = (node) n;

    /* Activate out buffers */
    unsigned int i;
    for (i = 0; i < nd->nb_outbufs; i++){
        setbufstat(nd, i, BUF_ACTIVE);
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

    if (launcher(&lf) == -1) lf_drop(&lf);

    /* Deactivate out buffers */
    for (i = 0; i < nd->nb_outbufs; i++){
        setbufstat(nd,i, BUF_INACTIVE);
    }

    return ret;
}



/* returns previous status */
int launch_node(node n){

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
                islot = new_inslot_l((struct out_buf*) n->input_slots[i]);
                break;
            case CIR_BUF:
                islot = new_inslot_c((struct out_buf*) n->input_slots[i]);
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
    err = pthread_create(&n->id, NULL, routine_wrapper, n);
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


int join_straph(straph s){
     
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

int lbuf_destroy(struct l_buf* b){
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

int cbuf_destroy(struct c_buf* b){
    free(b->buf);
    int err = pthread_mutex_destroy(&b->access_lock);
    if (err != 0){
        errno = err;
        return -1;
    }

    err = pthread_mutex_destroy(&b->nb_readers_mutex);
    if (err != 0){
        errno = err;
        return -1;
    }
    
    return 0;
}

int node_destroy(node n){
    unsigned int i;

    /* Destroy out bufs */
    if (n->output_buffers != NULL){
        for (i = 0; i < n->nb_outbufs; i++){
        
            if (n->output_buffers[i].buf == NULL) continue;

            switch (n->output_buffers[i].type){
                case LIN_BUF: lbuf_destroy(n->output_buffers[i].buf);
                    break;
                case CIR_BUF: cbuf_destroy(n->output_buffers[i].buf);
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
int straph_destroy(straph s){
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
        if (node_destroy(n) == -1) goto error;
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


int launch_straph(straph s){

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

    if (launcher(&lf) == -1){
        lf_drop(&lf);
        return -1;
    }

    return 0;
}





/**
 * Terminates the program printing an error message
 * @param msg Error message
 * @param line Number of the line
 * @param func Name of the function
 *
 * @note Do not use this function direclty, rather use
 *       the wrapping macro "fail(msg)
 */
void _fail(const char* msg, int line, const char* func){
    fprintf(stderr, "Error at line %d (function %s):\n",line,func);
    perror(msg);
    exit(EXIT_FAILURE);
}
#define fail(x) _fail(x, __LINE__, __func__)

int asd;
void* test(node n){
    char buf[10];
    size_t l = st_read(n,0,buf,10);
    printf("%d--> length: %ld \"%s\"\n",asd++,l,buf); 
    st_write(n,0,"hello",6);
    return NULL;
}



#define NN 10
int main(void){
    straph s = new_straph();
    node ns[NN];
   
 
    int i;
    for (i =0; i<NN; i++){
        ns[i] = new_node(test);
        if (ns[i] == NULL) fail("new_node");
    } 

    for (i=0; i< NN-1; i++){
        //XXX this should not be necessary
        if (set_buffer(ns[i], 0, LIN_BUF, 10) != 0) fail("set_buffer");
        if (link_nodes(ns[i],0,ns[i+1],0, PAR_MODE) != 0) fail("link_nodes");
    }
    
    if (add_start_node(s, ns[0]) != 0) fail("add_start_node");
    if (launch_straph(s) != 0) fail("launch_straph");
    if (join_straph(s) != 0) fail("join_straph");
    if (straph_destroy(s) != 0) fail("straph_destroy");

    
    return 0;
}


    
