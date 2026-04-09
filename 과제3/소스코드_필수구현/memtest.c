#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int pid;

  pid = fork();
  if(pid < 0){
    printf(1, "fork failed\n");
    exit();
  }

  if(pid == 0){
    char *args[] = { "memstress", "-n", "31", "-t", "500", 0 };
    exec("memstress", args);
    printf(1, "exec memstress failed\n");
    exit();
  }

  sleep(100);  

  int pid2 = fork();
  if(pid2 == 0){
    char *args2[] = { "memstress", "-n", "31", "-t", "500", 0 };
    exec("memstress", args2);
    printf(1, "exec memstress failed\n");
    exit();
  }

  sleep(100);  

  int pid3 = fork();
  if(pid3 < 0){
    printf(1, "fork failed\n");
    exit();
  }

  if(pid3 == 0){
    char *args3[] = { "memdump", "-p", "4", 0 };
    exec("memdump", args3);
    printf(1, "exec memdump failed\n");
    exit();
  }

  sleep(100);

  int pid4 = fork();
  if(pid4 < 0){
    printf(1, "fork failed\n");
    exit();
  }

  if(pid4 == 0){
    char *args4[] = { "memdump", "-p", "5", 0 };
    exec("memdump", args4);
    printf(1, "exec memdump failed\n");
    exit();
  }

  wait();
  wait();
  wait();
  wait();

  sleep(100);

  int pid5 = fork();
  if(pid5 < 0){
    printf(1, "fork failed\n");
    exit();
  }

  if(pid5 == 0){
    char *args5[] = { "memdump", "-p", "5", 0 };
    exec("memdump", args5);
    printf(1, "exec memdump failed\n");
    exit();
  }

  wait();  

  exit();
}
