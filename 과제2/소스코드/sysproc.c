#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

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


// sysproc.c
// 시스템 콜 핸들러: settickets(int tickets, int end_ticks)
// 유저 레벨에서 호출된 settickets() 요청을 처리하여
// 현재 프로세스의 티켓 수, stride 값, pass 값, end_ticks 등을 설정한다.
int
sys_settickets(void)
{
  int tickets, end_ticks;

  // 유저 프로그램으로부터 전달된 인자 읽기
  // argint(0, &tickets) : 첫 번째 인자 (티켓 수)
  // argint(1, &end_ticks) : 두 번째 인자 (종료 틱)
  if (argint(0, &tickets) < 0 || argint(1, &end_ticks) < 0)
    return -1;

  // 티켓 값 검증: 1 이상 STRIDE_MAX 이하
  if (tickets < 1 || tickets > STRIDE_MAX)
    return -1;

  // 현재 실행 중인 프로세스 정보 가져오기
  struct proc *curproc = myproc();

  // 프로세스에 티켓 수 반영
  curproc->tickets = tickets;

  // stride 값 계산 (티켓 수에 반비례)
  curproc->stride = STRIDE_MAX / tickets;

  // pass 값 초기화 (새로 설정된 프로세스는 0부터 시작)
  curproc->pass = 0;

  // end_ticks 값이 양수이면 해당 값으로 설정
  // end_ticks <= 0 인 경우는 무시
  if (end_ticks > 0)
    curproc->end_ticks = end_ticks;

  // 정상적으로 처리되었음을 반환
  return 0;
}



