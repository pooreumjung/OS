#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// -------------------------------------------------------------
// 프로세스 테이블 구조체
//  - 모든 프로세스 정보를 전역적으로 관리
//  - ptable.lock으로 보호됨 (동시 접근 방지)
// -------------------------------------------------------------
struct {
  struct spinlock lock;          // ptable 보호용 락
  struct proc proc[NPROC];       // 프로세스 배열 (최대 NPROC개)
} ptable;

static struct proc *initproc;    // 최초 init 프로세스 포인터
int nextpid = 1;                 // PID 자동 증가값

extern void forkret(void);
extern void trapret(void);
static void wakeup1(void *chan);

// -------------------------------------------------------------
// pinit()
//  - ptable 초기화 (락 이름 등록)
// -------------------------------------------------------------
void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// -------------------------------------------------------------
// cpuid(), mycpu(), myproc()
//  - 현재 실행 중인 CPU와 프로세스 정보를 반환
// -------------------------------------------------------------

// 인터럽트 비활성화 상태에서 호출해야 함
int cpuid() { return mycpu() - cpus; }

// 현재 CPU의 구조체 포인터 반환
struct cpu* mycpu(void)
{
  int apicid, i;
  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled");
  
  apicid = lapicid();
  for (i = 0; i < ncpu; ++i)
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  panic("unknown apicid");
}

// 현재 실행 중인 프로세스 포인터 반환
struct proc* myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();              // 인터럽트 비활성화
  c = mycpu();
  p = c->proc;            // 현재 CPU의 프로세스 포인터
  popcli();
  return p;
}

// -------------------------------------------------------------
// allocproc()
//  - UNUSED 상태의 엔트리를 찾아 EMBRYO로 변경
//  - 커널 스택과 컨텍스트를 초기화하여 실행 준비
// -------------------------------------------------------------
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);

  // 커널 스택 할당
  if ((p->kstack = kalloc()) == 0) {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // trap frame 공간 확보
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // trapret 주소 설정
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  // context 초기화 (forkret으로 점프)
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// -------------------------------------------------------------
// userinit()
//  - 최초의 사용자 프로세스(initcode.S)를 생성
// -------------------------------------------------------------
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();            // 프로세스 엔트리 확보
  initproc = p;

  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory");

  // initcode를 메모리에 적재
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;

  // 트랩프레임 초기화 (유저 모드 진입 설정)
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;             // initcode.S 시작 위치

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  acquire(&ptable.lock);
  p->state = RUNNABLE;        // 실행 가능 상태로 설정
  release(&ptable.lock);
}

// -------------------------------------------------------------
// growproc()
//  - 현재 프로세스의 메모리를 n바이트 확장/축소
//  - sbrk() 시스템콜에서 사용됨
// -------------------------------------------------------------
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0) {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if (n < 0) {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);   // 변경된 페이지 테이블 적용
  return 0;
}

// -------------------------------------------------------------
// fork()
//  - 부모 프로세스의 메모리, 파일 디스크립터, 이름 등을 복제
//  - copyuvm() 호출 시 COW 및 IPT 삽입 처리 수행
// -------------------------------------------------------------
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  if ((np = allocproc()) == 0)
    return -1;

  // 부모의 주소 공간 복사 (COW + IPT 포함)
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz, np)) == 0) {
    kfree(np->kstack);
    np->state = UNUSED;
    return -1;
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  np->tf->eax = 0;               // 자식 프로세스의 fork() 반환값 = 0

  // 열린 파일 복제
  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);

  return pid;
}

// -------------------------------------------------------------
// exit()
//  - 현재 프로세스 종료
//  - 열린 파일 정리 및 자식 init 재부모화
//  - IPT / SW-TLB 무효화는 freevm() 및 remove_pid() 단계에서 처리됨
// -------------------------------------------------------------
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // 열린 파일 닫기
  for (fd = 0; fd < NOFILE; fd++) {
    if (curproc->ofile[fd]) {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  wakeup1(curproc->parent);       // 부모 깨우기

  // 자식 프로세스 init으로 이관
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->parent == curproc) {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  curproc->state = ZOMBIE;        // 좀비 상태 전환
  sched();                        // 스케줄러 진입 (절대 반환되지 않음)
  panic("zombie exit");
}

// -------------------------------------------------------------
// wait()
//  - 자식 프로세스가 종료될 때까지 대기
// -------------------------------------------------------------
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for (;;) {
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->parent != curproc)
        continue;
      havekids = 1;

      // 종료된 자식 발견
      if (p->state == ZOMBIE) {
        pid = p->pid;
        kfree(p->kstack);
        freevm(p->pgdir);         // IPT 및 프레임 해제 포함
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    if (!havekids || curproc->killed) {
      release(&ptable.lock);
      return -1;
    }
    sleep(curproc, &ptable.lock);
  }
}

// -------------------------------------------------------------
// scheduler()
//  - 각 CPU가 실행하는 스케줄러 루프
//  - RUNNABLE 상태 프로세스를 찾아 RUNNING으로 전환
// -------------------------------------------------------------
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for (;;) {
    sti();                           // 인터럽트 허용
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state != RUNNABLE)
        continue;

      c->proc = p;
      switchuvm(p);                  // 페이지 디렉터리 전환
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context); // 문맥 전환
      switchkvm();                   // 커널 페이지 디렉터리 복귀
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

// -------------------------------------------------------------
// sched(), yield(), forkret()
//  - 스케줄러 전환 관련 함수
// -------------------------------------------------------------
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

void yield(void)
{
  acquire(&ptable.lock);
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

void forkret(void)
{
  static int first = 1;
  release(&ptable.lock);

  if (first) {
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
  // trapret으로 복귀 (allocproc에서 설정됨)
}

// -------------------------------------------------------------
// sleep(), wakeup(), kill()
//  - 동기화 및 프로세스 제어
// -------------------------------------------------------------
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  if (!p || !lk)
    panic("sleep");

  if (lk != &ptable.lock) {
    acquire(&ptable.lock);
    release(lk);
  }

  p->chan = chan;
  p->state = SLEEPING;
  sched();
  p->chan = 0;

  if (lk != &ptable.lock) {
    release(&ptable.lock);
    acquire(lk);
  }
}

static void wakeup1(void *chan)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// 프로세스 종료 요청 (kill)
int kill(int pid)
{
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// -------------------------------------------------------------
// procdump()
//  - 현재 프로세스 테이블 출력 (디버깅용)
// -------------------------------------------------------------
void
procdump(void)
{
  static char *states[] = {
    [UNUSED] "unused", [EMBRYO] "embryo",
    [SLEEPING] "sleep", [RUNNABLE] "runble",
    [RUNNING] "run", [ZOMBIE] "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    state = (p->state >= 0 && p->state < NELEM(states)) ? states[p->state] : "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING) {
      getcallerpcs((uint*)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

