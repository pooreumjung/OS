#include "types.h"
#include "stat.h"
#include "user.h"
#include "mmu.h"

#define MAX_ENTRIES 64

// ---- 플래그 출력 (P/W/U)
static void print_flags(uint16_t f) {
  int first = 1;
  if (f & PTE_P) { printf(1,"P"); first=0; }
  if (f & PTE_W) { printf(1,"%sW", first?"":"|"); first=0; }
  if (f & PTE_U) { printf(1,"%sU", first?"":"|"); first=0; }
  if (first) printf(1,"-");
}

// ---- phys2virt로 PA→(pid,va) 목록 확인
static void run_pfind(uint pa_page) {
  struct vlist out[MAX_ENTRIES];
  int n = phys2virt(pa_page, out, MAX_ENTRIES);
  if (n < 0) {
    printf(2,"  [pfind] phys2virt 실패\n");
    return;
  }
  printf(1,"  [pfind] phys2virt(0x%x): %d개 결과\n", pa_page, n);
  for (int i = 0; i < n; i++) {
    printf(1,"    pid=%d va=0x%x flags=0x%x (", out[i].pid, out[i].va_page, out[i].flags);
    print_flags(out[i].flags);
    printf(1,")\n");
  }
}

// ---- 종료된 PID 정리 확인
static int check_pid_removed(int pid_to_check, uint pa_full) {
  struct vlist results[MAX_ENTRIES];
  int count = phys2virt(pa_full, results, MAX_ENTRIES);
  if (count < 0) { printf(2,"    phys2virt 실패\n"); return -1; }
  if (count == 0) { printf(1,"    -> 참조 없음 (정리 성공)\n"); return 0; }

  int found = 0;
  for (int i = 0; i < count; i++) {
    if (results[i].pid == pid_to_check) found = 1;
  }
  if (found) {
    printf(2,"    -> 종료된 PID %d 매핑 남음 (정리 실패)\n", pid_to_check);
    return 1;
  } else {
    printf(1,"    -> 종료된 PID %d 매핑 없음 (정리 성공)\n", pid_to_check);
    return 0;
  }
}

// ===============================================================
//                       메인 테스트
// ===============================================================
int
main(void)
{
  uint mypid = getpid();
  printf(1,"\n===== IPT + COW Consistency Test (PID:%d) =====\n", mypid);

  // fork 전에 페이지 확보 (여유 2페이지)
  char *mem = sbrk(2 * PGSIZE);
  if (mem == (char*)-1) {
    printf(2,"페이지 할당 실패\n");
    exit();
  }

  memset(mem, 0, PGSIZE);
  *(volatile char*)mem = 0xAA;  // 매핑 강제 생성
  uint va = (uint)mem;

  //  fork 실행
  int child_pid = fork();
  if (child_pid < 0) {
    printf(2,"fork 실패\n");
    exit();
  }

  // ---------------- 자식 프로세스 ----------------
  if (child_pid == 0) {
    sleep(15); // 부모 먼저 출력
    mypid = getpid();
    printf(1,"  [자식:%d] 시작\n", mypid);

    uint pa_before = 0, fl_before = 0;
    if (vtop((void*)va, &pa_before, &fl_before) == 0) {
      printf(1,"  [자식] fork 직후: VA=0x%x → PA=0x%x flags=", va, pa_before);
      print_flags(fl_before);
      printf(1,"\n");
    }

    run_pfind(PGROUNDDOWN(pa_before));

    // COW 유도 (페이지 내부 안전영역)
    *(volatile uint*)(va + (PGSIZE - 8)) = 0xDEADBEEF;

    uint pa_after = 0, fl_after = 0;
    if (vtop((void*)va, &pa_after, &fl_after) == 0) {
      printf(1,"  [자식] write 후: VA=0x%x → PA=0x%x flags=", va, pa_after);
      print_flags(fl_after);
      printf(1,"\n");
    }

    run_pfind(PGROUNDDOWN(pa_after));

    if (pa_before == pa_after)
      printf(2,"  [자식] 경고: COW 미발생 (PA 동일)\n");

    printf(1,"  [자식] 종료\n");
    sleep(20); //  flush 대기 (printf/write 종료 보장)
    exit();
  }

  // ---------------- 부모 프로세스 ----------------
  else {
    printf(1,"  [부모:%d] 자식(PID:%d) 생성됨\n", mypid, child_pid);

    uint pa_parent = 0, fl_parent = 0;
    if (vtop((void*)va, &pa_parent, &fl_parent) == 0) {
      printf(1,"  [부모] fork 후: VA=0x%x → PA=0x%x flags=", va, pa_parent);
      print_flags(fl_parent);
      printf(1,"\n");
    }

    run_pfind(PGROUNDDOWN(pa_parent));

    //  자식 종료 대기
    wait();
    printf(1,"  [부모] 자식 종료 확인\n");

    //  IPT 정리 검사
    printf(1,"\n[Step2] 자식 PID %d 관련 잔존 매핑 검사\n", child_pid);
    check_pid_removed(child_pid, PGROUNDDOWN(pa_parent));

    //  부모 COW 유도 (안전한 페이지 내부)
    *(volatile uint*)(va + (PGSIZE - 8)) = 0xCAFEBABE;

    uint pa_after = 0, fl_after = 0;
    if (vtop((void*)va, &pa_after, &fl_after) == 0) {
      printf(1,"  [부모] write 후: VA=0x%x → PA=0x%x flags=", va, pa_after);
      print_flags(fl_after);
      printf(1,"\n");
    }

    run_pfind(PGROUNDDOWN(pa_after));

    printf(1,"\n===== IPT/COW Test 종료 =====\n");
    sleep(100); //  flush 보장
    exit();
  }
}

