#include "types.h"
#include "stat.h"
#include "user.h"

// Hit Rate 계산 함수 (정수 기반)
int calc_hit_rate(uint hits, uint misses) {
  return (hits + misses) ? (100 * hits / (hits + misses)) : 0;
}

int main(int argc, char *argv[]) {
  uint va = 0x1000; // 테스트 가상주소
  uint pa = 0;
  uint flags = 0;
  uint hits_before = 0, misses_before = 0;
  uint hits_after = 0, misses_after = 0;
  int re_executed = 0;
  int test_result = 0;

  if (argc == 2 && strcmp(argv[1], "--re-exec") == 0) {
    re_executed = 1;
  } else if (argc > 1 && strcmp(argv[1], "--re-exec") != 0) {
    printf(1, "참고: 주소 인자는 무시됩니다. 테스트 주소 0x%x 사용.\n", va);
  }

  printf(1, "===== C-3/C-4: SW-TLB 테스트 (PID: %d, Test VA: 0x%x) =====\n",
         getpid(), va);

  if (!re_executed) {
    // === Step 1: Hit/Miss 테스트 ===
    printf(1, "[Step 1] TLB Hit/Miss 테스트 시작...\n");

    get_tlb_stats(&hits_before, &misses_before);
    vtop((void*)va, &pa, &flags); // 첫 호출 (Miss 유도)
    get_tlb_stats(&hits_after, &misses_after);

    printf(1, "  - 통계 1: (호출 전 H=%d, M=%d) -> (1차 후 H=%d, M=%d)\n",
           hits_before, misses_before, hits_after, misses_after);

    if (misses_after <= misses_before) {
      printf(2, "  -> 오류: Miss가 증가하지 않음!\n");
      test_result = 1;
    } else {
      printf(1, "  -> 결과: TLB Miss 발생 (정상)\n");
    }

    int rate1 = calc_hit_rate(hits_after, misses_after);
    printf(1, "  -> 현재 Hit Rate: %d%%\n", rate1);

    hits_before = hits_after;
    misses_before = misses_after;

    vtop((void*)va, &pa, &flags); // 두 번째 호출 (Hit 유도)
    get_tlb_stats(&hits_after, &misses_after);

    printf(1, "  - 통계 2: (1차 후 H=%d, M=%d) -> (2차 후 H=%d, M=%d)\n",
           hits_before, misses_before, hits_after, misses_after);

    if (hits_after <= hits_before) {
      printf(2, "  -> 오류: Hit가 증가하지 않음! (캐시 실패?)\n");
      test_result = 1;
    } else {
      printf(1, "  -> 결과: TLB Hit 발생! (C-3 캐시 성공)\n");
    }

    int rate2 = calc_hit_rate(hits_after, misses_after);
    printf(1, "  -> 현재 Hit Rate: %d%%\n", rate2);

    // === Step 2: Exec 후 무효화 테스트 ===
    printf(1, "\n[Step 2] Exec 후 TLB 무효화 테스트 시작...\n");
    printf(1, "  - exec(\"swtlb_exec_test\", ...) 호출하여 재실행...\n");
    char *new_argv[] = { "swtlb_exec_test", "--re-exec", 0 };
    exec("swtlb_exec_test", new_argv);

    printf(2, "  - 오류: exec 실패!\n");
    test_result = 1;

  } else {
    // === Step 3: 재실행 후 무효화 검증 ===
    printf(1, "[Step 3] 재실행됨: TLB 무효화 확인 (vtop 호출)...\n");

    get_tlb_stats(&hits_before, &misses_before);
    printf(1, "  - 통계 3 (재실행 직후): H=%d, M=%d\n", hits_before, misses_before);

    vtop((void*)va, &pa, &flags);
    get_tlb_stats(&hits_after, &misses_after);

    printf(1, "  - 통계 4 (vtop 호출 후): H=%d, M=%d\n",
           hits_after, misses_after);

    if (misses_after <= misses_before) {
      printf(2, "  -> 오류: Miss가 증가하지 않음! (C-4 무효화 실패?)\n");
      test_result = 1;
    } else {
      printf(1, "  -> 결과: TLB Miss 발생! (C-4 무효화 성공)\n");
    }

    int rate3 = calc_hit_rate(hits_after, misses_after);
    printf(1, "  -> 현재 Hit Rate: %d%%\n", rate3);
  }

  // === 최종 결과 ===
  printf(1, "\n===== SW-TLB 테스트 종료 =====\n");
  if (test_result == 0)
    printf(1, ">> 결과: 성공 \n");
  else
    printf(2, ">> 결과: 실패 \n");

  exit();
}

