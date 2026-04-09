#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"

void freerange(void *vstart, void *vend);   // 지정된 주소 구간의 페이지를 free list에 등록
extern char end[];                          // 커널의 끝 주소 (링커 스크립트에서 정의됨)

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// 전역 물리 프레임 정보 테이블
struct physframe_info pftable[PFNNUM];

void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem"); // 메모리 락 초기화
  kmem.use_lock = 0;            // 잠금 비활성화
  freerange(vstart, vend);      // 지정 구간의 페이지들을 free list에 등록
}

void
kinit2(void *vstart, void *vend)
{
  // 전역 프레임 테이블 초기화
  for (int i = 0; i < PFNNUM; i++) {
    pftable[i].frame_index = i;
    pftable[i].allocated = 0;
    pftable[i].pid = -1;
    pftable[i].start_tick = 0;
  }

  // 전체 물리 페이지 영역을 free list에 등록
  freerange(vstart, vend);

  // 이후부터는 스핀락 보호 활성화
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  // 페이지 단위 정렬
  p = (char*)PGROUNDUP((uint)vstart);
  for (; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}

void
kfree(char *v)
{
  struct run *r;

  // 유효성 검사: 페이지 정렬, 커널영역 확인
  if ((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // 페이지를 1로 채움 (디버깅용)
  memset(v, 1, PGSIZE);

  // 필요 시 잠금 획득
  if (kmem.use_lock)
    acquire(&kmem.lock);

  // 전역 프레임 테이블 갱신
  if (kmem.use_lock) {
    uint frame_idx = V2P(v) / PGSIZE;   // 물리 프레임 번호 계산
    if (frame_idx < PFNNUM) {
      pftable[frame_idx].allocated = 0; // free 상태로 변경
      pftable[frame_idx].pid = -1;      // 소유 프로세스 없음
      pftable[frame_idx].start_tick = 0;// 사용 시작 tick 초기화
    }
  }

  // free list에 추가
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;

  if (kmem.use_lock)
    release(&kmem.lock);
}

char*
kalloc(void)
{
  struct run *r;

  // 스핀락 사용 중이면 락 획득
  if (kmem.use_lock)
    acquire(&kmem.lock);

  // free list에서 한 페이지 꺼내오기
  r = kmem.freelist;
  if (r) {
    kmem.freelist = r->next;

    int owner_pid = -1;
    uint start_tick = 0;

    // 현재 프로세스 및 시간 정보 확보
    if (kmem.use_lock) {
      struct proc *p = myproc();
      if (p)
        owner_pid = p->pid; // 현재 프로세스의 PID 저장

      acquire(&tickslock);
      start_tick = ticks;   // 현재 tick 값 저장
      release(&tickslock);
    }

    // 물리 프레임 인덱스 계산
    uint frame_idx = V2P((char*)r) / PGSIZE;

    // pftable에 기록
    if (frame_idx < PFNNUM) {
      pftable[frame_idx].frame_index = frame_idx;
      pftable[frame_idx].allocated = 1;
      pftable[frame_idx].pid = owner_pid;
      pftable[frame_idx].start_tick = start_tick;
    }
  }
  
  // 락 해제
  if (kmem.use_lock)
    release(&kmem.lock);
    
  return (char*)r;
}

int
dump_physmem_info(struct physframe_info *buf, int max)
{
  acquire(&kmem.lock);

  // 최대 PFNNUM 또는 요청된 max 중 작은 값만큼 복사
  uint n = (max < PFNNUM) ? max : PFNNUM;

  // 커널 내부 버퍼에서 직접 사용자 영역으로 복사
  if (copyout(myproc()->pgdir, (uint)buf, (char*)pftable, n * sizeof(struct physframe_info)) < 0) {
    release(&kmem.lock);
    return -1;
  }

  release(&kmem.lock);
  return n;
}

