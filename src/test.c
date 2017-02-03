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


    
