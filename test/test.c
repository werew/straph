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

#include <unistd.h>
int asd;
void* test(node n){
    size_t l = 0;
    size_t w = 0;
    char buf[10] = {0x0};
    w = st_write(n,0,"hello",6);
    l = st_read(n,0,buf,6);
    printf("%d--> write: %ld read: %ld \"%s\"\n",asd++,w,l,buf); 
    return NULL;
}

#define AM 10
void* test2(node n){
    long int s = 0;
    puts("node2");

    while (s < AM){
        s++;
        puts("--node2 write--");
        st_write(n,0,&s,sizeof s);
/*        puts("--node2 read--");
        st_read(n,0,&s,sizeof s); 
        printf("%ld <--\n",s); */
    }
    return NULL;
}

void* test3(node n){
    long int s = 0;
    puts("node3");

    while (s < AM){
        puts("--node3 read--");
        st_read(n,0,&s,sizeof s);
        printf("    --> %ld\n",s);
      /*  s++;
        puts("--node3 write--");
        st_write(n,0,&s,sizeof s); */
    }
    return NULL;
}



#define NN 10
int main(void){
    straph s = st_create();
/*
    node ns[NN];
   
    int i;
    asd = 0;
    for (i =0; i<NN; i++){
        ns[i] = st_makenode(test);
        if (ns[i] == NULL) fail("makenode");
    } 

    for (i=0; i< NN-1; i++){
        if (st_setbuffer(ns[i], 0, CIR_BUF, 100) != 0) fail("st_setbuffer");
        if (st_nlink(ns[i],ns[i+1], PAR_MODE) != 0) fail("link_nodes");
        if (st_addflow(ns[i],0,ns[i+1],0) != 0) fail("st_addflow");
    }
    
    if (st_addnode(s, ns[0]) != 0) fail("addnode");
    if (st_start(s) != 0) fail("st_start");
    if (st_join(s) != 0) fail("st_join");
    if (st_destroy(s) != 0) fail("straph_destroy");
*/

    node n2 = st_makenode(test2);
    node n3 = st_makenode(test3);
    st_setbuffer(n2, 0, CIR_BUF, 120);
    st_setbuffer(n3, 0, CIR_BUF, 120);
    st_addflow(n2,0,n3,0);
    st_addflow(n3,0,n2,0);
    st_addnode(s, n2);
    st_addnode(s, n3); 
    st_start(s);
    st_join(s);
    
    return 0;
}


    
