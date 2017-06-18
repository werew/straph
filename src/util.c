#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "straph.h"



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
    void *el;
    struct lf_cell* f = lf->first;
    if (f == NULL){
        errno = ENOENT;
        return NULL;
    }

    lf->first = f->next;
    if (lf->last == f) lf->last = f->next;

    el = f->element;
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
