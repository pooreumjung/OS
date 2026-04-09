#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(1, "Usage: snap_rollback id\n");
    exit();
  }

  int id = atoi(argv[1]);

  if(snapshot_rollback(id) < 0){
    printf(1, "snapshot_rollback failed\n");
    exit();
  }

  exit();
}

