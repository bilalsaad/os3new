
#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096 
#define COUNT  20 
char* m[COUNT];
  int
main(int argc, char *argv[])
{
  int i = 0,j = 0;
  char c = 'a';
  char cc[] = {0,0,0,0,0,0,0,0,0,0,0};
  printf(1, "GOTOTOTOTOTO \n");
  while (i < COUNT) {
    m[i] = sbrk(PGSIZE);
    printf(1, "allocated page %d at %p \n", i, m[i]);
    i++;
  }
  printf(1, "finsihed creating pages \n");
  gets(cc, 10);
  for(i = 0; i < COUNT; ++i) {
    printf(1, "reading from page %d \n", i);
    for(j = 0; j < PGSIZE; ++j) {
      m[i][j] = c; 
    }
  }


  printf(1, "finished  %d!\n", c);
  gets(cc, 10);
  exit();
}

void mem2(){

  void *m1, *m2;
  m1 = 0;

  printf(1, "mem2 test\n");

  while((m2 = malloc(10001)) != 0){
    *(char**)m2 = m1;
    m1 = m2;
  }
  while(m1){
    m2 = *(char**)m1;
    free(m1);
    m1 = m2;
  }
  m1 = malloc(1024*20);
  if(m1 == 0){
    printf(1, "couldn't allocate mem?!!\n");
    exit();
  }
  free(m1);
  printf(1, "mem2 ok\n");
  exit();
}

  void
mem(void)
{
  void *m1, *m2;
  int pid, ppid;

  printf(1, "mem test\n");
  ppid = getpid();
  if((pid = fork()) == 0){
    m1 = 0;
    while((m2 = malloc(10001)) != 0){
      *(char**)m2 = m1;
      m1 = m2;
    }
    while(m1){
      m2 = *(char**)m1;
      free(m1);
      m1 = m2;
    }
    m1 = malloc(1024*20);
    if(m1 == 0){
      printf(1, "couldn't allocate mem?!!\n");
      kill(ppid);
      exit();
    }
    free(m1);
    printf(1, "mem ok\n");
    exit();
  } else {
    wait();
  }
}

