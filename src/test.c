#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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

int asd;
void* test(node n){
    char buf[10];
    size_t l = st_read(n,0,buf,10);
    printf("%d--> length: %ld \"%s\"\n",asd++,l,buf); 
    st_write(n,0,"hello",6);
    return NULL;
}

void* topotest(node arg){
    puts("TOPOO");
    arg++;
    return NULL;
}

#define NN 10
int main(void){
    straph s = st_create();
    node ns[NN];
   
    node tt; 
    int i;
    for (i =0; i<NN; i++){
        ns[i] = st_makenode(test);
        if (ns[i] == NULL) fail("makenode");
    } 

    for (i=0; i< NN-1; i++){
        if (st_setbuffer(ns[i], 0, LIN_BUF, 10) != 0) fail("st_setbuffer");
        if (st_nlink(ns[i],ns[i+1], SEQ_MODE) != 0) fail("link_nodes");
        if (st_addflow(ns[i],0,ns[i+1],0) != 0) fail("st_addflow");
    }
    tt = st_makenode(topotest);
    st_nlink(tt,ns[i],SEQ_MODE); 
    
    if (st_addnode(s, ns[0]) != 0) fail("addnode");
    if (st_addnode(s, tt) != 0) fail("addnode");
    if (st_start(s) != 0) fail("st_start");
    if (st_join(s) != 0) fail("st_join");
    if (st_destroy(s) != 0) fail("straph_destroy");

    
    return 0;
}


    
