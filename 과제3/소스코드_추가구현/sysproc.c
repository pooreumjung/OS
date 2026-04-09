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
#include "vm.h"

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

int
sys_vtop(void)
{
  void *va;                // 사용자가 전달한 가상 주소를 저장할 변수
  uint32_t *pa_out;      // 사용자가 결과를 받기 위해 전달한 (물리 주소) 포인터
  uint32_t *flags_out;   // 사용자가 결과를 받기 위해 전달한 (플래그) 포인터
  
  // sw_vtop의 결과를 커널 공간에 임시로 저장할 변수
  uint32_t pa_val;
  uint32_t flags_val;

  // 1. 사용자 스택에서 시스템 콜 인자들을 가져옵니다.
  // argptr은 사용자 공간 포인터(주소)를 안전하게 읽어옵니다.
  // (0)번째 인자: va (가상 주소)
  // (1)번째 인자: pa_out (물리 주소를 저장할 사용자 포인터)
  // (2)번째 인자: flags_out (플래그를 저장할 사용자 포인터)
  if(argptr(0, (char**)&va, sizeof(void*)) < 0 ||
     argptr(1, (char**)&pa_out, sizeof(uint32_t*)) < 0 ||
     argptr(2, (char**)&flags_out, sizeof(uint32_t*)) < 0)
    return -1; // 인자 가져오기 실패

  // 2. 핵심 커널 로직(sw_vtop)을 호출합니다.
  //    - myproc()->pgdir: 현재 실행 중인 프로세스의 페이지 디렉터리
  //    - va: 사용자가 요청한 가상 주소
  //    - &pa_val, &flags_val: 결과를 저장할 커널 변수의 주소
  if(sw_vtop(myproc()->pgdir, va, &pa_val, &flags_val) < 0)
    return -1; // 페이지 변환 실패

  // 3. 커널 공간의 결과(pa_val, flags_val)를
  //    사용자 공간 포인터(pa_out, flags_out)가 가리키는 위치로 복사합니다.
  //    copyout은 커널->사용자 공간으로의 안전한 메모리 복사를 수행합니다.
  if(copyout(myproc()->pgdir, (uint)pa_out, &pa_val, sizeof(uint32_t)) < 0 ||
     copyout(myproc()->pgdir, (uint)flags_out, &flags_val, sizeof(uint32_t)) < 0)
    return -1; // 사용자 공간으로 결과 복사 실패

  // 모든 과정이 성공하면 0을 반환합니다.
  return 0;
}

// (C-5) IPT를 위한 시스템 콜 (PFN 변환 추가)
// int sys_phys2virt(uint32_t pa_page, struct vlist *out, int max);
int
sys_phys2virt(void)
{
  uint32_t pa_full;  // 사용자로부터 받은 전체 물리 주소
  uint32_t pfn;      // 계산된 물리 프레임 번호 (PFN)
  char *addr;        // 사용자 공간 포인터 (struct vlist *)
  int max_entries;
  struct proc *p = myproc();
  struct vlist *kbuf; // 커널 임시 버퍼
  int count;

  // 1. 인자 파싱 (pa_full은 전체 주소)
  if(argint(0, (int*)&pa_full) < 0 || 
     argint(2, &max_entries) < 0)
    return -1;
  
  if (max_entries <= 0)
    return 0;

  // argptr로 사용자 버퍼 주소와 크기 검증
  if(argptr(1, &addr, max_entries * sizeof(struct vlist)) < 0)
    return -1;

  // --- PFN 변환 추가 ---
  // 2. 받은 전체 주소(pa_full)를 PFN으로 변환 (오프셋 제거)
  pfn = pa_full >> 12; // 또는 pa_full / PGSIZE
  // --- PFN 변환 끝 ---

  // 3. 커널 임시 버퍼 할당
  int max_in_buf = PGSIZE / sizeof(struct vlist);
  if (max_entries > max_in_buf)
    max_entries = max_in_buf;
    
  kbuf = (struct vlist*)kalloc();
  if(kbuf == 0)
    return -1;
    
  // 4. IPT 검색 함수 호출 (PFN 사용)
  count = ipt_find(pfn, kbuf, max_entries); // pa_full 대신 pfn 전달

  // 5. 결과를 사용자 공간으로 복사
  if (count > 0) {
    if(copyout(p->pgdir, (uint)addr, (char*)kbuf, count * sizeof(struct vlist)) < 0) {
      kfree((char*)kbuf);
      return -1;
    }
  }

  kfree((char*)kbuf);
  return count; // 복사된 엔트리 개수 반환
}


int
sys_get_tlb_stats(void)
{
  uint hits_addr;
  uint misses_addr;
  struct proc *p = myproc();

  uint khits, kmisses; 

  if (argint(0, (int*)&hits_addr) < 0 || argint(1, (int*)&misses_addr) < 0) {
    return -1;
  }

  swtlb_get_stats(&khits, &kmisses); // sw_tlb.c 함수 호출

  if (copyout(p->pgdir, hits_addr, (char*)&khits, sizeof(uint)) < 0) {
    return -1;
  }
  if (copyout(p->pgdir, misses_addr, (char*)&kmisses, sizeof(uint)) < 0) {
    return -1;
  }

  return 0; 
}
