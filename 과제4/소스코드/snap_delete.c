#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(1, "Usage: snap_delete id\n");
    exit();
  }

  int id = atoi(argv[1]);
  if(snapshot_delete(id) < 0){
    printf(1, "snapshot_delete failed\n");
    exit();
  }

  exit();
}

