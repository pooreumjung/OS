#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "fs.h"   // NDIRECT, NINDIRECT

#define MAXBLK 300

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(1, "usage: print_addr <file>\n");
    exit();
  }

  int blocks[MAXBLK];
  int n = get_block_list(argv[1], blocks);

  if(n < 0){
    printf(1, "print_addr failed\n");
    exit();
  }

  //
  // 단계 1) DIRECT block 출력 (addr[0..11])
  //
  for(int i=0; i<NDIRECT && i<n; i++){
    if(blocks[i])
      printf(1, "addr[%d] : %x\n", i, blocks[i]);
  }

  //
  // 단계 2) INDIRECT POINTER 출력 (addr[12])
  //
  if(n > NDIRECT && blocks[NDIRECT]){
    printf(1, "addr[%d] : %x (INDIRECT POINTER)\n",
           NDIRECT, blocks[NDIRECT]);
  } else {
    // indirect pointer가 없는 경우 → direct만 있는 파일
    exit();
  }

  //
  // 단계 3) INDIRECT DATA BLOCK 출력
  // blocks 배열에서 NDIRECT+1 부터가 indirect data block.
  //
  int idx = NDIRECT + 1;      // indirect data block 시작 index
  int sub = 0;                // 간접 블록 내부 offset

  while(idx < n){
    printf(1, "addr[%d] -> [%d] (bn : %d) : %x\n",
           NDIRECT, sub, NDIRECT + sub, blocks[idx]);

    idx++;
    sub++;
  }

  exit();
}

