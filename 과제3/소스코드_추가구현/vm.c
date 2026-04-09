#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];               // kernel.ld에서 정의된 커널 데이터 시작 주소
pde_t *kpgdir;                    // 커널 페이지 디렉터리 (스케줄러 등에서 사용)

// -----------------------------------------------------------
// seginit()
//   - 각 CPU별 GDT(Global Descriptor Table) 초기화
//   - 커널 코드/데이터 및 사용자 코드/데이터 세그먼트 디스크립터 설정
// -----------------------------------------------------------
void
seginit(void)
{
  struct cpu *c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);           // 커널 코드 세그먼트
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);                 // 커널 데이터 세그먼트
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);    // 유저 코드 세그먼트
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);          // 유저 데이터 세그먼트
  lgdt(c->gdt, sizeof(c->gdt));                                     // GDT 로드
}

// -----------------------------------------------------------
// walkpgdir()
//   - 주어진 가상주소 va에 대한 PTE 포인터를 반환
//   - alloc=1이면 존재하지 않는 경우 새 페이지 테이블을 할당
// -----------------------------------------------------------
pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde = &pgdir[PDX(va)];
  pte_t *pgtab;

  if (*pde & PTE_P)
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));        // 페이지 디렉터리 엔트리에서 테이블 주소 획득
  else {
    // alloc이 0이거나 kalloc 실패 시 NULL 반환
    if (!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    memset(pgtab, 0, PGSIZE);
    // 새 페이지 테이블 생성 후 PDE 업데이트 (읽기/쓰기/유저 접근 허용)
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// -----------------------------------------------------------
// mappages()
//   - [va, va+size) 범위를 물리주소 pa부터 매핑
//   - 유저 공간 매핑 시 IPT 삽입 수행
// -----------------------------------------------------------
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a = (char*)PGROUNDDOWN((uint)va);
  char *last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  pte_t *pte;

  for (;;) {
    if ((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if (*pte & PTE_P)
      panic("remap");                      // 이미 존재하면 오류
    *pte = pa | perm | PTE_P;              // 물리주소와 권한 플래그 설정

    // 유저 공간 매핑 시 IPT에 삽입
    if ((uint)a < KERNBASE) {
      struct proc *p = myproc();
      if (p)
        ipt_insert((uint)a, pa, p->pid, PTE_FLAGS(*pte));
    }

    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// -----------------------------------------------------------
// KMAP 구조체: 커널의 고정 매핑 테이블
// -----------------------------------------------------------
static struct kmap {
  void *virt;      // 가상주소 시작
  uint phys_start; // 물리주소 시작
  uint phys_end;   // 물리주소 끝
  int perm;        // 접근 권한 비트
} kmap[] = {
  { (void*)KERNBASE, 0,             EXTMEM,    PTE_W },
  { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0 },
  { (void*)data,     V2P(data),     PHYSTOP,   PTE_W },
  { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W },
};

// -----------------------------------------------------------
// setupkvm()
//   - 커널 전용 페이지 디렉터리 생성 및 고정 매핑 적용
// -----------------------------------------------------------
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if ((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);

  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");

  // kmap에 정의된 영역들을 순서대로 매핑
  for (k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if (mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                 (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// -----------------------------------------------------------
// kvmalloc(), switchkvm()
//   - 커널 페이지 디렉터리 생성 및 커널 모드 활성화
// -----------------------------------------------------------
void kvmalloc(void) { kpgdir = setupkvm(); switchkvm(); }
void switchkvm(void) { lcr3(V2P(kpgdir)); }

// -----------------------------------------------------------
// switchuvm()
//   - 유저 프로세스 실행을 위한 TSS 및 페이지 디렉터리 설정
// -----------------------------------------------------------
void
switchuvm(struct proc *p)
{
  if (p == 0 || p->kstack == 0 || p->pgdir == 0)
    panic("switchuvm: invalid process");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  mycpu()->ts.iomb = (ushort)0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));           // 해당 프로세스의 페이지 디렉터리로 전환
  popcli();
}

// -----------------------------------------------------------
// inituvm()
//   - PID 1(init 프로세스)의 첫 페이지 초기화
//   - IPT에 (pid=1, va=0, pa) 삽입
// -----------------------------------------------------------
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;
  if (sz >= PGSIZE)
    panic("inituvm: more than a page");

  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);

  ipt_insert(0, V2P(mem), 1, PTE_W|PTE_U|PTE_P);   // IPT 등록

  memmove(mem, init, sz); // 초기 프로그램 복사
}

// -----------------------------------------------------------
// loaduvm()
//   - ELF 실행 파일의 segment를 물리 메모리에 로드
// -----------------------------------------------------------
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if ((uint)addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");

  for (i = 0; i < sz; i += PGSIZE) {
    if ((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    n = (sz - i < PGSIZE) ? (sz - i) : PGSIZE;
    if (readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// -----------------------------------------------------------
// allocuvm()
//   - 프로세스 주소 공간 확장 (sbrk(+))
//   - 새 페이지 할당 후 IPT에 삽입
// -----------------------------------------------------------
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if (newsz >= KERNBASE)
    return 0;
  if (newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for (; a < newsz; a += PGSIZE) {
    mem = kalloc();
    if (mem == 0) {
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0) {
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// -----------------------------------------------------------
// deallocuvm()
//   - 프로세스 주소 공간 축소 (sbrk(-))
//   - 물리 프레임 해제 전 IPT에서 제거
// -----------------------------------------------------------
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if (newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for (; a < oldsz; a += PGSIZE) {
    pte = walkpgdir(pgdir, (char*)a, 0);
    if (!pte)
      a = PGADDR(PDX(a)+1, 0, 0) - PGSIZE;
    else if (*pte & PTE_P) {
      pa = PTE_ADDR(*pte);
      if (pa == 0)
        panic("kfree");

      struct proc *p = myproc();
      if (p && p->pgdir == pgdir)
        ipt_remove(a, pa, p->pid);       // IPT에서 제거

      kfree(P2V(pa));                    // 물리 페이지 해제
      *pte = 0;
    }
  }
  return newsz;
}

// -----------------------------------------------------------
// freevm()
//   - 프로세스 종료 시 전체 주소 공간 해제
// -----------------------------------------------------------
void
freevm(pde_t *pgdir)
{
  if (pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for (int i = 0; i < NPDENTRIES; i++) {
    if (pgdir[i] & PTE_P)
      kfree(P2V(PTE_ADDR(pgdir[i])));
  }
  kfree((char*)pgdir);
}

// -----------------------------------------------------------
// clearpteu()
//   - 유저 스택 가드 페이지 보호용 (User 접근 금지)
// -----------------------------------------------------------
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte = walkpgdir(pgdir, uva, 0);
  if (pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;     // 유저 접근 비트 제거
}

// -----------------------------------------------------------
// copyuvm()
//   - 부모 프로세스 주소 공간을 복제하여 자식 프로세스 생성
//   - COW(Copy-on-Write) 처리 및 IPT 삽입 포함
// -----------------------------------------------------------
pde_t*
copyuvm(pde_t *pgdir, uint sz, struct proc *child)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;

  if ((d = setupkvm()) == 0)
    return 0;

  for (i = 0; i < sz; i += PGSIZE) {
    if ((pte = walkpgdir(pgdir, (void*)i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if (!(*pte & PTE_P))
      continue;

    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);

    // (1) 쓰기 가능 페이지를 COW로 변환
    if (flags & PTE_W) {
      uint newflags = (flags & ~PTE_W) | PTE_COW | PTE_P | (flags & PTE_U);
      *pte = pa | newflags;
      lcr3(V2P(pgdir));    // TLB flush
      flags = newflags;
      cprintf("[COW SET] va=0x%x flags=0x%x\n", i, newflags);
    }

    // (2) 물리 프레임 참조 카운트 증가
    incr_refcount(pa);

    // (3) 자식 프로세스 매핑 생성
    if (mappages(d, (void*)i, PGSIZE, pa, flags) < 0)
      goto bad;

    // (4) IPT 삽입 (init 프로세스는 제외)
    if (child && child->pid > 1)
      ipt_insert(i, pa, child->pid, flags);
  }

  // (5) TLB 동기화
  lcr3(V2P(pgdir));
  lcr3(V2P(d));
  return d;

bad:
  freevm(d);
  return 0;
}

// -----------------------------------------------------------
// uva2ka()
//   - 유저 가상주소(uva)를 커널 접근 가능한 물리주소로 변환
// -----------------------------------------------------------
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte = walkpgdir(pgdir, uva, 0);
  if (!(*pte & PTE_P) || !(*pte & PTE_U))
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// -----------------------------------------------------------
// copyout()
//   - 커널 → 유저 주소 공간으로 len 바이트 복사
// -----------------------------------------------------------
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf = (char*)p, *pa0;
  uint n, va0;

  while (len > 0) {
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if (n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

// -----------------------------------------------------------
// sw_vtop()
//   - (C-3 단계) SW-TLB 기반 가상→물리주소 변환
//   - 하드웨어 접근 없이 PTE를 파싱해 pa 및 flags 계산
//   - 캐시 히트 시 swtlb_lookup()으로 즉시 반환
// -----------------------------------------------------------
int
sw_vtop(pde_t *pgdir, const void *va, uint32_t *pa_out, uint32_t *pte_flags_out)
{
  struct proc *p = myproc();
  uint pid = p ? p->pid : 0;

  // (1) SW-TLB에서 먼저 검색 (히트 시 즉시 반환)
  if (pid && swtlb_lookup(pid, (uint)va, pa_out, pte_flags_out))
    return 0;

  // (2) PDE/PTE 직접 탐색
  pde_t *pde = &pgdir[PDX(va)];
  if (!(*pde & PTE_P))
    return -1;
  pte_t *pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  pte_t *pte = &pgtab[PTX(va)];
  if (!(*pte & PTE_P))
    return -1;

  // (3) 물리주소 및 플래그 계산
  uint pa = PTE_ADDR(*pte) + ((uint)va & 0xFFF);
  uint flags = PTE_FLAGS(*pte);
  *pa_out = pa;
  *pte_flags_out = flags;

  // (4) SW-TLB에 삽입 (miss 시)
  if (pid)
    swtlb_insert(pid, (uint)va, pa, flags);

  return 0;
}

