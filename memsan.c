
#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096
#define COUNT  20
char* m[COUNT];
  int
main(int argc, char *argv[])
{
  int i = 0,j = 0, pid;
  char c = 'a';
  //char cc[] = {0,0,0,0,0,0,0,0,0,0,0};

  while (i < COUNT) {
    m[i] = sbrk(PGSIZE);
    printf(1, "allocated page %d at %p \n", i, m[i]);
    i++;
  }
  for(i = 0; i < COUNT; ++i) {
    for(j = 0; j < PGSIZE; ++j) {
      m[i][j] = c+10; 
    }
  }
  printf(1, "finsihed creating pages \n");
  if ((pid = fork()) == 0) {
    c = 'b';
  }
     
/*  for(i = 0; i < COUNT; ++i) {
    for(j = 0; j < PGSIZE; ++j) {
      m[i][j] = c; 
    }
  }*/
  
  wait(); 
  printf(1, "VALUES FOR %s \n", (pid == 0) ? "child" : "parent");
  for(i = 0; i < COUNT; ++i) {
    printf(1, "page %d has value %c \n", i, m[i][0]);
  }
 

  printf(1, "finished  %c!\n", c);
  exit();
}
