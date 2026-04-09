#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"

// add headerfile
#include "fs.h"      
#include "file.h"
#include "fcntl.h"
#include "buf.h"


int sys_fork(void) { return fork(); }

int sys_exit(void) {
  exit();
  return 0;
}

int sys_wait(void) { return wait(); }

int sys_kill(void) {
  int pid;
  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int sys_getpid(void) { return myproc()->pid; }

int sys_sbrk(void) {
  int addr;
  int n;
  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int sys_sleep(void) {
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

int sys_uptime(void) {
  uint xticks;
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_get_block_list(void)
{
  char *path;
  int *ubuf;      // user-space 주소 (int 배열로 사용됨)
  struct inode *ip;

  // 사용자로부터 path 문자열 가져오기
  if (argstr(0, &path) < 0)
    return -1;

  // 사용자 배열 주소 가져오기 (메모리 접근 검증 포함)
  // argptr 은 user-space pointer 유효성 검증 + kernel에서 접근 허용
  if (argptr(1, (char**)&ubuf, sizeof(int) * 150) < 0)
    return -1;

  //  2) path → inode 획득
  ip = namei(path);
  if (ip == 0)
    return -1;

  ilock(ip);   // inode 내부 읽기 위해 lock 필요

  int blocks[150];   // 직접 관리 가능한 커널 스택 배열
  int cnt = 0;

  for (int i = 0; i < NDIRECT; i++) {
    if (ip->addrs[i])
      blocks[cnt++] = ip->addrs[i];
  }

  // 4) INDIRECT BLOCK 처리
  if (ip->addrs[NDIRECT]) {
    uint indir_blk = ip->addrs[NDIRECT];

    // 4-1) Indirect pointer block 자체를 기록
    blocks[cnt++] = indir_blk;

    // 4-2) Indirect block 내부의 Data block 번호 로드
    struct buf *bp = bread(ip->dev, indir_blk);
    uint *a = (uint*)bp->data;

    //  5) INDIRECT BLOCK 내부의 DATA BLOCK 수집
    for (int j = 0; j < NINDIRECT; j++) {
      if (a[j])
        blocks[cnt++] = a[j];
    }

    brelse(bp);
  }

  iunlock(ip);
  iput(ip);

  // 6) Kernel → User-space 배열 복사
  if (copyout(myproc()->pgdir, (uint)ubuf,
              (char*)blocks, sizeof(int) * cnt) < 0)
    return -1;

  return cnt;
}

