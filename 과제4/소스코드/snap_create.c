#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int id = snapshot_create();
  if(id < 0){
    printf(1, "snapshot_create failed\n");
    exit();
  } else{
    printf(1, "snapshot id = %d\n", id);
  }
  exit();
}

