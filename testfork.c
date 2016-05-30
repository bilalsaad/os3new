#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    int n=6;
    void * pages[n];
    
    int pid1;
    int pid2;
    //char cc[10]; 
    pid1=fork();
    
    int i;
    for (i=0; i<n; i++) {
        pages[i]=sbrk(4096);
    }
    
    for (i=0; i<n; i++) {
        *(int *)pages[i] = i;
    }
    pid2=fork();
    
    printf(1,"-------------------------------------------AFTER SECOND FORK\n");
    
    for (i=0; i<n; i++) {
       printf(1,"\n\n\n------------------TEST MASSIVE %d   pid %d\n\n\n", *((int *)pages[i]),getpid() );
    }
     printf(1,"\n\n\n-----------------------------------------------------BEFORE WAIT\n\n\n\n");
   
    if(pid1>0)
      wait();
    
    if (pid2>0)
      wait();
  printf(1, "%d %d \n ", pid1, pid2);
  
   printf(1,"--------------------------AFTER WAIT\n");
   exit();
}
 
 /*
#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

#define PGSIZE 4096
#define COUNT 20

char* m1[COUNT];

volatile int
main(int argc, char *argv[])
{
    
    int i,j;
    fork();
    //creating 'COUNT' pages
    for (i = 0; i < COUNT ; ++i)
    {
        m1[i] = sbrk(PGSIZE);
        printf(1, "allocated page #%d at address: %x\n", i, m1[i]);
    }
    fork();
    //using all pages
    for ( i = 0; i < COUNT; ++i)
    {
        for ( j = 0; j < PGSIZE; ++j)
        {
            m1[i][j] = 0;
        }
    }
    
    printf(1,"Finished Successfuly!!!\n");
    
    exit();
    return 0;
}
*/
