#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

struct gatedesc idt[256];
extern uint vectors[];
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;
  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

// ----------------------------------------------------------
// (C-2) Copy-on-Write Fault Handler
// ----------------------------------------------------------
static int
handle_cow_fault(uint faultva)
{
  struct proc *p = myproc();
  uint va_pg = PGROUNDDOWN(faultva);
  pte_t *pte = walkpgdir(p->pgdir, (char*)va_pg, 0);
  if (!pte) return -1;

  if (!(*pte & PTE_P) || !(*pte & PTE_U) || !(*pte & PTE_COW))
    return -1;

  uint pa = PTE_ADDR(*pte);
  uint pfn = pa / PGSIZE;
  uint oflg = PTE_FLAGS(*pte);
  int shared = (pfn < PFNNUM && pfn_refcnt[pfn] > 1);

  if (shared) {
    // (1) 새 페이지 할당
    char *mem = kalloc();
    if (!mem) return -1;
    memmove(mem, (char*)P2V(pa), PGSIZE);

    // (2) 기존 매핑 정리 및 refcount 감소
    ipt_remove(va_pg, pa, p->pid);
    swtlb_invalidate(p->pid, va_pg);
    decr_refcount(pa); // refcount 감소 누락 방지

    // (3) 새 PTE 생성 (PTE_U 반드시 포함)
    uint nflg = ((oflg | PTE_W | PTE_P | PTE_U) & ~PTE_COW);
    *pte = V2P(mem) | nflg;

    // (4) 새 매핑 등록 후 flush
    lcr3(V2P(p->pgdir));
    ipt_insert(va_pg, V2P(mem), p->pid, nflg);

  } else {
    uint nflg = ((oflg | PTE_W | PTE_P | PTE_U) & ~PTE_COW);
    *pte = pa | nflg;
    ipt_update_flags(p->pid, va_pg, nflg);
    lcr3(V2P(p->pgdir));
  }

  return 0;
}

// ----------------------------------------------------------
// Trap Handler
// ----------------------------------------------------------
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;

  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;

  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;

  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;

  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    lapiceoi();
    break;

  // ----------------------------------------------------------
  // (C-2) Page Fault Handler (User mode only)
  // ----------------------------------------------------------
  case T_PGFLT:
  {
    uint va = rcr2();
    struct proc *p = myproc();
    pte_t *pte = 0;

    if(p)
      pte = walkpgdir(p->pgdir, (void*)va, 0);

    // 유저 모드에서 발생한 COW 폴트만 처리
    if(p && (tf->cs & 3) == DPL_USER && pte &&
       (*pte & PTE_P) && (*pte & PTE_U) &&
       !(*pte & PTE_W) && (*pte & PTE_COW))
    {
      handle_cow_fault(va);
      break;
    }

    // 나머지 페이지 폴트는 일반 trap으로 처리
  }

  default:
    if(myproc() == 0 || (tf->cs & 3) == 0){
      cprintf("unexpected trap %d (cr2=0x%x)\n",
              tf->trapno, rcr2());
      panic("trap");
    }
    cprintf("pid %d %s: trap %d err %d on cpu %d eip 0x%x addr 0x%x\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // 프로세스 종료 조건 확인
  if(myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();

  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0 + IRQ_TIMER)
    yield();

  if(myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();
}

