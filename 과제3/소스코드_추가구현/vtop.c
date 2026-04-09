#include "types.h"
#include "stat.h"
#include "user.h" 

// 16진수 문자열("0x...")을 정수로 변환하는 함수
uint hextou(char *s)
{
  uint n = 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
  }
  while (*s) {
    char c = *s;
    if (c >= '0' && c <= '9') {
      n = (n * 16) + (c - '0');
    } else if (c >= 'a' && c <= 'f') {
      n = (n * 16) + (c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      n = (n * 16) + (c - 'A' + 10);
    } else {
      break;
    }
    s++;
  }
  return n;
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf(2, "사용법: vtop <가상주소(16진수)>\n");
    printf(2, "예시: vtop 0x1000\n");
    exit();
  }

  // 16진수 변환 함수(hextou) 사용
  uint va = hextou(argv[1]);
  uint pa = 0;
  uint flags = 0;
  uint hits1 = 0, misses1 = 0;
  uint hits2 = 0, misses2 = 0;

  printf(1, "--- C-3: SW-TLB 테스트 시작 ---\n");

  // 1. 첫 번째 통계 수집 (초기 상태)
  if (get_tlb_stats(&hits1, &misses1) < 0) {
    printf(2, "get_tlb_stats 호출 실패\n");
    exit();
  }
  printf(1, "[1] vtop 호출 전: Hits = %d, Misses = %d\n", hits1, misses1);

  // 2. vtop (C-1) 시스템 콜 호출 (TLB Miss 유도)
  printf(1, "[2] vtop(0x%x) 1차 호출 중...\n", va);
  if (vtop((void*)va, &pa, &flags) < 0) {
    printf(1, "    -> vtop 실패 (매핑되지 않은 주소)\n");
  } else {
    // printf는 %x로 16진수 출력이 바로 가능합니다.
    printf(1, "    -> vtop 성공: PA=0x%x, Flags=0x%x\n", pa, flags);
  }

  // 3. 두 번째 통계 수집 (첫 번째 호출 후)
  get_tlb_stats(&hits2, &misses2);
  printf(1, "[3] vtop 1차 호출 후: Hits = %d, Misses = %d\n", hits2, misses2);

  if (misses2 > misses1) {
    printf(1, "    -> 결과: TLB Miss 발생 (정상)\n");
  } else {
    printf(1, "    -> 결과: TLB Miss가 발생하지 않음 (오류 또는 이미 캐시됨)\n");
  }

  // 4. 동일한 주소로 vtop 재호출 (TLB Hit 유도)
  printf(1, "[4] vtop(0x%x) 2차 호출 중...\n", va);
  vtop((void*)va, &pa, &flags); // 캐시 히트를 유도 (결과 출력은 생략)

  // 5. 세 번째 통계 수집 (두 번째 호출 후)
  uint hits3 = 0, misses3 = 0;
  get_tlb_stats(&hits3, &misses3);
  printf(1, "[5] vtop 2차 호출 후: Hits = %d, Misses = %d\n", hits3, misses3);

  if (hits3 > hits2) {
    printf(1, "    -> 결과: TLB Hit 발생! (C-3 캐시 성공)\n");
  } else {
    printf(1, "    -> 결과: TLB Hit가 발생하지 않음 (C-3 캐시 실패)\n");
  }

  exit();
}
