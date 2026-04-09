#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h" // spinlock 구조체 정의를 위해 추가
#include "types.h"

// kalloc.c의 kmem 구조체를 외부 변수로 선언 
// 'kmem' undeclared 오류를 해결
extern struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// pftable만 사용하도록 pf_info 선언 제거 
// kalloc.c에서 사용하는 pftable만 참조
extern struct physframe_info pftable[];

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
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

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


int
sys_dump_physmem_info(void)
{
  void *addr;
  int max_entries;

  // 사용자 인자 유효성 검사
  // argptr(0): 첫 번째 인자 (addr)를 포인터로 읽음
  // argint(1): 두 번째 인자 (max_entries)를 정수로 읽음
  if(argptr(0, (void*)&addr, sizeof(void*)) < 0 || argint(1, &max_entries) < 0)
    return -1;

  // 복사할 최대 엔트리 수를 PFNNUM (전체 프레임 수)로 제한
  int entries_to_copy = PFNNUM;
  if (max_entries < entries_to_copy)
    entries_to_copy = max_entries;

  // 프레임 테이블은 동시 접근 가능성이 있으므로 락 획득
  acquire(&kmem.lock);

  // 커널 → 사용자 공간으로 안전하게 복사
  // 실패 시 -1 반환
  if(copyout(myproc()->pgdir, (uint)addr, (char*)pftable,
             sizeof(struct physframe_info) * entries_to_copy) < 0){
    release(&kmem.lock);
    return -1;
  }

  // 복사 완료 후 락 해제
  release(&kmem.lock);

  // 성공적으로 복사한 엔트리 개수 반환
  return entries_to_copy;
}


