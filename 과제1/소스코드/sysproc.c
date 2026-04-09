#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h" // 스핀락 구조체 정의 포함
#include "proc.h"

// 'ptable'은 proc.c에 전역으로 정의된 프로세스 테이블이다.
// sysproc.c에서도 접근하기 위해 extern으로 선언해 참조한다.
extern struct {
  struct spinlock lock;       // 동시 접근 제어를 위한 스핀락
  struct proc proc[NPROC];    // 실제 프로세스 배열
} ptable;

// ---------------------- 기존 xv6 시스템 콜 ----------------------

int sys_fork(void) {
  return fork(); // 새로운 프로세스 생성
}

int sys_exit(void) {
  exit();       // 현재 프로세스 종료
  return 0;     // 도달 불가 (형식상 반환)
}

int sys_wait(void) {
  return wait(); // 자식 프로세스 종료 대기
}

int sys_kill(void) {
  int pid;
  if(argint(0, &pid) < 0) // 첫 번째 인자(PID) 가져오기
    return -1;
  return kill(pid);       // 해당 PID 프로세스 강제 종료
}

int sys_getpid(void) {
  return myproc()->pid;   // 현재 프로세스의 PID 반환
}

int sys_sbrk(void) {
  int addr;
  int n;
  if(argint(0, &n) < 0)   // 힙 증가 크기 읽기
    return -1;
  addr = myproc()->sz;    // 현재 메모리 크기 저장
  if(growproc(n) < 0)     // 힙 영역 확장 시도
    return -1;
  return addr;            // 확장 전 크기 반환
}

int sys_sleep(void) {
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)   // sleep할 시간 읽기
    return -1;
  acquire(&tickslock);    // ticks 변수 보호
  ticks0 = ticks;
  while(ticks - ticks0 < n){   // 지정 시간만큼 대기
    if(myproc()->killed){      // 프로세스가 kill된 경우 중단
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock); // 조건변수 sleep
  }
  release(&tickslock);
  return 0;
}

// 커널 시작 이후 발생한 클럭 인터럽트 횟수 반환
int sys_uptime(void) {
  uint xticks;
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// ---------------------- 새로 추가한 시스템 콜 ----------------------

// hello_number 시스템 콜 구현
// 인자로 정수를 받아 커널 콘솔에 출력하고, 두 배 값을 반환한다.
int sys_hello_number(void) {
  int n;
  if(argint(0, &n) < 0)   // 첫 번째 인자 읽기
    return -1;
  cprintf("Hello, xv6! Your number is %d\n", n); // 커널 콘솔 출력
  return n * 2;           // 계산 결과 반환 (유저 공간에서 받음)
}

// ---------------------- get_procinfo 구현 ----------------------

// 커널 내부에서 사용하는 프로세스 정보 구조체
// (유저 영역에 복사되기 전, 커널 공간에서 임시로 채움)
struct k_procinfo {
  int  pid, ppid, state;  // 프로세스 ID, 부모 PID, 상태
  uint sz;                // 메모리 크기
  char name[16];          // 프로세스 이름
};

// get_procinfo 시스템 콜 구현
// 특정 PID의 프로세스 정보를 구조체로 채워 유저 프로그램에 전달한다.
int sys_get_procinfo(void) {
  int pid;
  char *uaddr;                  // 유저 버퍼 시작 주소
  struct proc *p, *t;
  struct k_procinfo kinfo;

  // 첫 번째 인자로 pid 값 읽기
  if(argint(0, &pid) < 0) return -1;
  // 두 번째 인자로 유저 버퍼 주소 읽기
  if(argptr(1, &uaddr, sizeof(struct k_procinfo)) < 0) return -1;

  acquire(&ptable.lock);        // 프로세스 테이블 접근 보호

  if(pid <= 0) 
    t = myproc();               // pid <= 0이면 자기 자신 정보
  else {
    t = 0;
    // 프로세스 테이블에서 해당 pid 탐색
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->pid == pid) { t = p; break; }
  }

  // 해당 pid가 없거나 프로세스가 UNUSED 상태라면 실패
  if(t == 0 || (t->state == UNUSED)) { 
    release(&ptable.lock); 
    return -1; 
  }

  // 커널 내부 구조체에 프로세스 정보 채우기
  kinfo.pid   = t->pid;
  kinfo.ppid  = (t->parent ? t->parent->pid : 0); // 부모 없으면 0
  kinfo.state = t->state;
  kinfo.sz    = t->sz;
  safestrcpy(kinfo.name, t->name, sizeof(kinfo.name)); // 이름 복사

  release(&ptable.lock);        // 락 해제

  // 유저 버퍼로 구조체 복사
  if(copyout(myproc()->pgdir, (uint)uaddr, (void*)&kinfo, sizeof(kinfo)) < 0)
    return -1;

  return 0; // 성공
}

