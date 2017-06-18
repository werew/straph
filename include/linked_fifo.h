#ifndef __LINKED_FIFO_H__
#define __LINKED_FIFO_H__



/* Linked fifo's cell */
struct lf_cell {
    void* element;         /* Content of the cell */
    struct lf_cell* next;  /* Next linked cell    */
};


/* Linked fifo */
struct linked_fifo {
    struct lf_cell* first; /* Least recent cell of the list */
    struct lf_cell* last;  /* Most recent cell of the list  */
};

#endif
