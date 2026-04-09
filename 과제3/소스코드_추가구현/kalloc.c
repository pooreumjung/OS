#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h" // C-2: myproc() 사용을 위해 추가

void freerange(void *vstart, void *vend);
extern char end[];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock; // freelist용 락
  int use_lock;
  struct run *freelist;
  
  // <-- (C-2) COW 참조 카운트 배열 추가 -->
  #define TOTAL_PAGES (PHYSTOP / PGSIZE)
  uint refcount[TOTAL_PAGES]; // 각 물리 페이지의 참조 카운트
  struct spinlock reflock;    // 참조 카운트 배열 보호용 락
} kmem;

// (과제 A) 전역 물리 프레임 정보 테이블
struct physframe_info pftable[PFNNUM]; // PFNNUM은 param.h 등에 정의 필요

// (과제 A) tickslock 선언 추가 (kalloc에서 사용)
extern struct spinlock tickslock;
extern uint ticks;


void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem"); // freelist 락 초기화
  
  // <-- (C-2) 참조 카운트 락 초기화 -->
  initlock(&kmem.reflock, "kmem_ref");
  // <-- (C-2) 추가 끝 -->
  
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  // (과제 A) 전역 프레임 테이블 초기화
  for (int i = 0; i < PFNNUM; i++) {
    pftable[i].frame_index = i;
    pftable[i].allocated = 0;
    pftable[i].pid = -1;
    pftable[i].start_tick = 0;
  }

  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for (; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p); // kfree 내부에서 refcount=0으로 초기화됨 (아래 kfree 수정 참조)
}

// (C-2: 참조 카운트 로직 추가됨)
void
kfree(char *v)
{
  struct run *r;

  if ((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // <-- (C-2) 참조 카운트 기반 kfree로 수정 -->
  // 1. 참조 카운트를 1 감소시키고, 0이 되었는지 확인
  if (kmem.use_lock && decr_refcount(V2P(v)) == 0) {
    // 2. 카운트가 0이 아니면 (아직 공유 중이면) 아무것도 하지 않고 반환
    return; 
  }
  // 3. 카운트가 0이 되었거나, 초기화 중(use_lock=0)일 때만 실제 메모리 해제 로직 수행
  //    (초기화 중 freerange에서 호출될 때는 refcount가 0이어야 함)
  // <-- (C-2) 수정 끝 -->

  // (과제 A) 전역 프레임 테이블 갱신 (참조 카운트가 0일 때만)
  if (kmem.use_lock) {
    uint frame_idx = V2P(v) / PGSIZE;
    if (frame_idx < PFNNUM) {
      // (락 순서 주의: kmem.lock 전에 다른 락(reflock)을 잡으면 안 됨)
      // (간단하게 하기 위해 여기서는 pftable 업데이트 시 별도 락 없음)
      pftable[frame_idx].allocated = 0; 
      pftable[frame_idx].pid = -1;     
      pftable[frame_idx].start_tick = 0;
    }
  }

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock); // freelist 락 획득
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock); // freelist 락 해제
}

// (C-2: 참조 카운트 로직 추가됨)
char*
kalloc(void)
{
  struct run *r;

  if (kmem.use_lock)
    acquire(&kmem.lock); // freelist 락 획득
  r = kmem.freelist;
  if (r) {
    kmem.freelist = r->next;

    // <-- (C-2) 할당 시 참조 카운트 1로 설정 -->
    if(kmem.use_lock)
      acquire(&kmem.reflock); // ref 락 획득
      
    uint pa = V2P((char*)r);
    if(kmem.refcount[pa / PGSIZE] != 0) // 안전 체크: free 페이지의 카운트는 0이어야 함
       panic("kalloc: refcount not zero");
    kmem.refcount[pa / PGSIZE] = 1; // 카운트 1로 설정
    
    if(kmem.use_lock)
      release(&kmem.reflock); // ref 락 해제
    // <-- (C-2) 추가 끝 -->


    // (과제 A) 전역 프레임 테이블 갱신
    if (kmem.use_lock) {
      int owner_pid = -1;
      uint start_tick = 0;
      struct proc *p = myproc();
      if (p)
        owner_pid = p->pid;

      acquire(&tickslock);
      start_tick = ticks;
      release(&tickslock);
      
      uint frame_idx = pa / PGSIZE;
      if (frame_idx < PFNNUM) {
        // (락 순서 주의: kmem.lock 안에서 다른 락(reflock, tickslock) 잡음)
        // (간단하게 하기 위해 여기서는 pftable 업데이트 시 별도 락 없음)
        pftable[frame_idx].frame_index = frame_idx;
        pftable[frame_idx].allocated = 1;
        pftable[frame_idx].pid = owner_pid;
        pftable[frame_idx].start_tick = start_tick;
      }
    }
  }
  
  if (kmem.use_lock)
    release(&kmem.lock); // freelist 락 해제
    
  // 할당 성공 시 페이지 0으로 초기화 (선택 사항)
  // if(r)
  //   memset((char*)r, 0, PGSIZE);
    
  return (char*)r;
}


// (과제 A) 시스템 콜 구현
int
dump_physmem_info(struct physframe_info *buf, int max)
{
  // (락 순서 주의: kmem.lock 안에서 copyout 호출 시 페이지 폴트 발생 가능성)
  // (안전하게 하려면 pftable용 별도 락 사용 또는 데이터 임시 복사 필요)
  
  acquire(&kmem.lock); // 임시: kmem.lock 사용

  uint n = (max < PFNNUM) ? max : PFNNUM;

  if (copyout(myproc()->pgdir, (uint)buf, (char*)pftable, n * sizeof(struct physframe_info)) < 0) {
    release(&kmem.lock);
    return -1;
  }

  release(&kmem.lock);
  return n;
}


// --- (C-2) 참조 카운트 함수 정의 ---

// (C-2) 물리 페이지 참조 카운트 1 증가 (fork/copyuvm 시 호출)
void
incr_refcount(uint pa)
{
  if (pa >= PHYSTOP)
    panic("incr_refcount: pa out of range");

  acquire(&kmem.reflock);
  if (kmem.refcount[pa / PGSIZE] < 1) // 할당되지 않은 페이지의 카운트를 증가시키려 함
    panic("incr_refcount: refcounting non-allocated page");
    
  kmem.refcount[pa / PGSIZE]++;
  release(&kmem.reflock);
}

// (C-2) 물리 페이지 참조 카운트 1 감소 (kfree 내부에서 호출됨)
// 0이 되면 1을 반환 (실제 free 필요), 0이 아니면 0을 반환 (free 불필요)
int
decr_refcount(uint pa)
{
  if (pa >= PHYSTOP)
    panic("decr_refcount: pa out of range");

  acquire(&kmem.reflock);
  if (kmem.refcount[pa / PGSIZE] < 1) // 이미 free된 페이지의 카운트를 감소시키려 함
    panic("decr_refcount: refcounting free page");
    
  kmem.refcount[pa / PGSIZE]--;
  int should_free = (kmem.refcount[pa / PGSIZE] == 0);
  release(&kmem.reflock);
  
  return should_free; // 1이면 free 필요, 0이면 아직 공유 중
}


// (C-2) 물리 페이지 참조 카운트 반환 (handle_cow_fault에서 호출)
uint
get_refcount(uint pa)
{
  if (pa >= PHYSTOP)
    panic("get_refcount: pa out of range");
  
  uint count;
  acquire(&kmem.reflock);
  count = kmem.refcount[pa / PGSIZE];
  release(&kmem.reflock);
  return count;
}
// --- (C-2) 참조 카운트 함수 정의 끝 ---
