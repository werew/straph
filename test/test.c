#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "straph.h"

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

void* nodewrite(node n){
    char buf = 'A';
    st_write(n,0,&buf,1);
    return NULL;
}

void* readandprint(node n){
    char buf = 0;
    st_read(n,0,&buf,1);
    printf("%c\n",buf);
    if (buf == 'A') return NULL;
    else return (void*) 1;
}

int main(void){
    straph s = st_create();
    node n1 = st_makenode(nodewrite);
    node n2 = st_makenode(readandprint);
    int ret = 0;

    st_addnode(s, n1);
    st_nlink(n1,n2,SEQ_MODE);
    st_setbuffer(n1,0,LIN_BUF,1);
    st_addflow(n1,0,n2,0);
    st_start(s);
    st_join(s);

    if (n1->ret != 0 || n2->ret != 0) 
        ret = EXIT_FAILURE;
    st_destroy(s);
    return ret;
}


    
