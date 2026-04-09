#include "types.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "defs.h"

struct spinlock ipt_lock;          // IPT 해시 테이블 보호용 락
struct ipt_entry *ipt_hash[IPT_BUCKETS]; // 해시 테이블

struct spinlock ipt_pool_lock;     // 엔트리 풀 보호용 락
struct ipt_entry ipt_pool[MAX_IPT_ENTRIES];
struct ipt_entry *ipt_freelist;    // 사용 가능한 엔트리 연결 리스트

int pfn_refcnt[PFNNUM];            // PFN별 참조 카운트 (Copy-on-Write 추적)

// ----------------------------------------------------------------------
// 해시 인덱스 계산 함수 (pid + va 기반)
// ----------------------------------------------------------------------
static inline uint
ipt_hash_index_pidva(uint pid, uint va)
{
  uint va_pg = PGROUNDDOWN(va);
  // Knuth’s multiplicative hash: 충돌 최소화
  uint key = pid ^ (va_pg * 2654435761u);
  return key % IPT_BUCKETS;
}

// ----------------------------------------------------------------------
// IPT 초기화
//  - 부팅 시 main.c의 kinit2 이후 호출됨
// ----------------------------------------------------------------------
void
ipt_init(void)
{
	initlock(&ipt_lock, "ipt_lock");
	initlock(&ipt_pool_lock, "ipt_pool_lock");

	acquire(&ipt_lock);
	for (int i = 0; i < IPT_BUCKETS; i++)
		ipt_hash[i] = 0;	// 해시 버킷 초기화
	release(&ipt_lock);
	
	acquire(&ipt_pool_lock);
	ipt_freelist = 0;
	for (int i = 0; i < MAX_IPT_ENTRIES; i++) {
		ipt_pool[i].next = ipt_freelist; // freelist 연결
		ipt_freelist = &ipt_pool[i];
	}
	for(int i=0;i<PFNNUM;i++) 
		pfn_refcnt[i]=0; // PFN 참조 카운트 초기화
	release(&ipt_pool_lock);
}

// ----------------------------------------------------------------------
// ipt_insert()
//  - (pid, va) 기반으로 IPT에 새 매핑 추가
//  - 이미 존재하는 경우 flags만 갱신
// ----------------------------------------------------------------------
int
ipt_insert(uint va, uint pa, uint pid, uint flags)
{
  uint pfn = pa / PGSIZE;
  if (pfn >= PFNNUM) return -1;

  uint index = ipt_hash_index_pidva(pid, va);

  acquire(&ipt_lock);
  for (struct ipt_entry *c = ipt_hash[index]; c; c = c->next) {
    if (c->pid == pid && c->va == PGROUNDDOWN(va) && c->pfn == pfn) {
      c->flags = flags;  // 이미 존재하면 flags만 갱신
      release(&ipt_lock);
      return 0;
    }
  }
  release(&ipt_lock);

  // 새 엔트리 할당
  struct ipt_entry *e;
  acquire(&ipt_pool_lock);
  e = ipt_freelist;
  if (!e) { release(&ipt_pool_lock); panic("ipt_insert: pool empty"); }
  ipt_freelist = e->next;
  release(&ipt_pool_lock);

  e->pfn = pfn;
  e->pid = pid;
  e->va = PGROUNDDOWN(va);
  e->flags = flags;

  // 해시 체인에 삽입
  acquire(&ipt_lock);
  pfn_refcnt[pfn]++;
  e->refcnt = pfn_refcnt[pfn];
  e->next = ipt_hash[index];
  ipt_hash[index] = e;
  release(&ipt_lock);

  return 0;
}

// ----------------------------------------------------------------------
// ipt_remove()
//  - (pid, va) 기반으로 IPT에서 엔트리 제거
//  - PFN 참조 카운트 감소 및 풀 반환
// ----------------------------------------------------------------------
int
ipt_remove(uint va, uint pa, uint pid)
{
  uint pfn = pa / PGSIZE;
  uint index = ipt_hash_index_pidva(pid, va);

  acquire(&ipt_lock);
  struct ipt_entry *prev = 0, *cur = ipt_hash[index];

  while (cur) {
    if (cur->pfn == pfn && cur->pid == pid && cur->va == PGROUNDDOWN(va)) {
      if (prev) prev->next = cur->next;
      else ipt_hash[index] = cur->next;

      if (pfn < PFNNUM && pfn_refcnt[pfn] > 0)
        pfn_refcnt[pfn]--;

      release(&ipt_lock);

      acquire(&ipt_pool_lock);
      cur->next = ipt_freelist;
      ipt_freelist = cur;
      release(&ipt_pool_lock);
      return 0;
    }
    prev = cur;
    cur = cur->next;
  }
  release(&ipt_lock);
  return -1;
}

// ----------------------------------------------------------------------
// ipt_remove_pid()
//  - 특정 PID가 가진 모든 매핑 제거 (프로세스 종료 시 호출)
// ----------------------------------------------------------------------
void
ipt_remove_pid(uint pid)
{
  for (int i = 0; i < IPT_BUCKETS; i++) {
    struct ipt_entry *recycle = 0;

    acquire(&ipt_lock);
    struct ipt_entry *prev = 0, *cur = ipt_hash[i];
    while (cur) {
      if (cur->pid == pid) {
        struct ipt_entry *victim = cur;
        uint pfn = victim->pfn;
        cur = cur->next;

        if (prev) prev->next = cur;
        else ipt_hash[i] = cur;

        if (pfn < PFNNUM && pfn_refcnt[pfn] > 0)
          pfn_refcnt[pfn]--;

        victim->next = recycle;
        recycle = victim;
        continue;
      }
      prev = cur;
      cur = cur->next;
    }
    release(&ipt_lock);

    // 풀로 반환
    if (recycle) {
      acquire(&ipt_pool_lock);
      while (recycle) {
        struct ipt_entry *n = recycle->next;
        recycle->next = ipt_freelist;
        ipt_freelist = recycle;
        recycle = n;
      }
      release(&ipt_pool_lock);
    }
  }
}

// ----------------------------------------------------------------------
// ipt_update_flags()
//  - (pid, va) 기반으로 IPT 엔트리의 flags 갱신
//  - Copy-on-Write 후 권한 변경 등에 사용
// ----------------------------------------------------------------------
int
ipt_update_flags(uint pid, uint va, uint16_t newflags)
{
  uint index = ipt_hash_index_pidva(pid, va);

  acquire(&ipt_lock);
  for (struct ipt_entry *e = ipt_hash[index]; e; e = e->next) {
    if (e->pid == pid && e->va == PGROUNDDOWN(va)) {
      e->flags = newflags;
      release(&ipt_lock);
      return 0;
    }
  }
  release(&ipt_lock);
  return -1; // 해당 항목 없음
}

// ----------------------------------------------------------------------
// ipt_find()
//  - 주어진 PFN에 매핑된 (pid, va, flags) 목록 반환
//  - phys2virt() 시스템콜에서 사용
// ----------------------------------------------------------------------
int
ipt_find(uint32_t pfn, struct vlist *out_buffer, int max_entries)
{
  int count = 0;

  acquire(&ipt_lock);
  for (int i = 0; i < IPT_BUCKETS; i++) {
    for (struct ipt_entry *e = ipt_hash[i]; e; e = e->next) {
      if (e->pfn == pfn) {
        if (count < max_entries) {
          out_buffer[count].pid = e->pid;
          out_buffer[count].va_page = e->va;
          out_buffer[count].flags = e->flags;
          count++;
        } else {
          release(&ipt_lock);
          return count;
        }
      }
    }
  }
  release(&ipt_lock);
  return count;
}


