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
    printf(2, "사용법: pfind <물리주소(16진수)>\n");
    printf(2, "예시: pfind 0xDEE000\n");
    exit();
  }

  // 1. 16진수 물리 주소를 파싱합니다.
  //    (물리 주소 전체를 받지만, 시스템 콜은 PFN을 기대할 수 있습니다.
  //     일단 전체 주소를 보내고, 커널에서 PFN(>>12)을 사용한다고 가정합니다.)
  uint pa_page = hextou(argv[1]);
  
  // 2. 결과를 저장할 버퍼를 선언합니다. (최대 64개 항목)
  #define MAX_ENTRIES 64
  struct vlist results[MAX_ENTRIES];
  int count;

  printf(1, "--- C-2: IPT 테스트 (pfind) 시작 ---\n");
  printf(1, "[1] phys2virt(0x%x) 호출 중...\n", pa_page);

  // 3. C-2에서 구현한 phys2virt 시스템 콜을 호출합니다.
  count = phys2virt(pa_page, results, MAX_ENTRIES);

  if (count < 0) {
    printf(2, "[2] phys2virt 호출 실패\n");
    exit();
  }

  if (count == 0) {
    printf(1, "[2] 해당 물리 주소를 참조하는 프로세스가 없습니다.\n");
    exit();
  }

  printf(1, "[2] 총 %d개의 매핑을 찾았습니다:\n", count);

  // 4. 커널에서 받아온 결과를 출력합니다.
  for (int i = 0; i < count; i++) {
    printf(1, "    -> PID: %d, VA: 0x%x, Flags: 0x%x\n",
           results[i].pid,
           results[i].va_page,
           results[i].flags);
  }

  exit();
}
