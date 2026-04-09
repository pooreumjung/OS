#include "types.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

// ----------------------------------------------------------------------
// (C-3, C-4) Software TLB 구현 파일
//  - 최근 참조된 (pid, va → pa) 매핑을 캐시하여 vtop() 속도를 향상
//  - IPT 기반 조회보다 빠른 캐시 접근을 통해 소프트웨어 페이지 워커 성능 개선
//  - exec() 및 프로세스 종료 시 TLB 무효화(invalidate) 수행
// ----------------------------------------------------------------------

#define SWTLB_SIZE 512                   // TLB 엔트리 개수 (2^9 = 512)
#define SWTLB_MASK (SWTLB_SIZE - 1)      // 인덱스 마스크 (모듈러 연산 대체)

// ----------------------------------------------------------------------
// Software TLB 엔트리 구조체 정의
//  - pid, va, pa, flags 정보를 저장
//  - valid 비트는 캐시 유효 여부를 나타냄
// ----------------------------------------------------------------------
struct swtlb_entry {
  int valid;        // 1: 유효, 0: 무효
  uint pid;         // 프로세스 ID (TLB 분리 지원)
  uint va_page;     // 페이지 단위의 가상 주소 (하위 12비트 제외)
  uint pa_page;     // 페이지 단위의 물리 주소 (하위 12비트 제외)
  uint flags;       // 해당 PTE의 접근 권한 비트 (P/W/U/COW 등)
};

// 전역 캐시 테이블 및 동기화용 락, 통계 변수
static struct swtlb_entry swtlb[SWTLB_SIZE]; // Direct-mapped 캐시 배열
static struct spinlock swtlb_lock;           // 캐시 접근 동기화를 위한 스핀락
static uint swtlb_hits, swtlb_misses;        // 성능 통계 (조회 결과 카운트)

// ----------------------------------------------------------------------
// swtlb_index()
//  - (pid, va_page) 조합으로 해시 인덱스를 계산
//  - Knuth의 multiplicative hash로 충돌 최소화
// ----------------------------------------------------------------------
static inline uint
swtlb_index(uint pid, uint va_pg)
{
  return (pid ^ (va_pg * 2654435761u)) & SWTLB_MASK; // XOR + 곱셈 기반 해싱
}

// ----------------------------------------------------------------------
// swtlb_init()
//  - SW-TLB 테이블 및 통계 초기화
//  - 부팅 시 main.c의 kinit() 이후 호출됨
// ----------------------------------------------------------------------
void
swtlb_init(void)
{
  initlock(&swtlb_lock, "swtlb");        // 스핀락 초기화
  acquire(&swtlb_lock);
  memset(swtlb, 0, sizeof(swtlb));       // 모든 엔트리 무효화
  swtlb_hits = swtlb_misses = 0;         // 통계 초기화
  release(&swtlb_lock);

#ifdef DEBUG
  cprintf("[swtlb] initialized (%d entries)\n", SWTLB_SIZE);
#endif
}

// ----------------------------------------------------------------------
// swtlb_lookup()
//  - (pid, va)에 대한 캐시 조회
//  - Hit: pa_out / flags_out을 채우고 1 반환
//  - Miss: 통계 증가 후 0 반환
// ----------------------------------------------------------------------
int
swtlb_lookup(uint pid, uint va, uint *pa_out, uint *flags_out)
{
  uint va_pg = PGROUNDDOWN(va);         // 페이지 단위 정렬
  uint idx = swtlb_index(pid, va_pg);   // 인덱스 계산
  int hit = 0;

  acquire(&swtlb_lock);
  struct swtlb_entry *e = &swtlb[idx];

  // 캐시 히트 조건: 유효 + 동일한 PID/VA_PAGE
  if (e->valid && e->pid == pid && e->va_page == va_pg) {
    if (pa_out) *pa_out = e->pa_page + (va & 0xFFF); // 오프셋 포함 계산
    if (flags_out) *flags_out = e->flags;
    swtlb_hits++;
    hit = 1;
  } else {
    swtlb_misses++;
  }
  release(&swtlb_lock);
  return hit;
}

// ----------------------------------------------------------------------
// swtlb_insert()
//  - 새로운 (pid, va → pa) 매핑을 캐시에 삽입
//  - 동일 인덱스 충돌 시 기존 항목 덮어씀 (direct-mapped 구조)
// ----------------------------------------------------------------------
void
swtlb_insert(uint pid, uint va, uint pa, uint flags)
{
  uint va_pg = PGROUNDDOWN(va);
  uint pa_pg = PGROUNDDOWN(pa);
  uint idx = swtlb_index(pid, va_pg);

  acquire(&swtlb_lock);
  // 구조체 리터럴로 한 번에 엔트리 초기화
  swtlb[idx] = (struct swtlb_entry){1, pid, va_pg, pa_pg, flags};
  release(&swtlb_lock);
}

// ----------------------------------------------------------------------
// swtlb_invalidate()
//  - 특정 (pid, va) 항목만 무효화
//  - 페이지 해제나 권한 변경 시 호출됨
// ----------------------------------------------------------------------
void
swtlb_invalidate(uint pid, uint va)
{
  uint va_pg = PGROUNDDOWN(va);
  uint idx = swtlb_index(pid, va_pg);

  acquire(&swtlb_lock);
  struct swtlb_entry *e = &swtlb[idx];
  if (e->valid && e->pid == pid && e->va_page == va_pg)
    e->valid = 0;  // 무효화 처리
  release(&swtlb_lock);
}

// ----------------------------------------------------------------------
// swtlb_invalidate_pid()
//  - 특정 PID의 모든 엔트리를 무효화
//  - 프로세스 종료(exit) 또는 exec() 시 호출
// ----------------------------------------------------------------------
void
swtlb_invalidate_pid(uint pid)
{
  acquire(&swtlb_lock);
  for (int i = 0; i < SWTLB_SIZE; i++) {
    if (swtlb[i].valid && swtlb[i].pid == pid)
      swtlb[i].valid = 0; // 해당 프로세스의 캐시 삭제
  }
  release(&swtlb_lock);
}

// ----------------------------------------------------------------------
// swtlb_get_stats()
//  - SW-TLB의 히트/미스 통계 조회
//  - 디버깅 및 성능 분석용 (예: /proc/swtlb_stats 출력)
// ----------------------------------------------------------------------
void
swtlb_get_stats(uint *hits, uint *misses)
{
  acquire(&swtlb_lock);
  if (hits) *hits = swtlb_hits;
  if (misses) *misses = swtlb_misses;
  release(&swtlb_lock);
}

