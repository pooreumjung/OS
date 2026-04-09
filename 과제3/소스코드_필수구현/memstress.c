#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

// 잘못된 사용법을 알려주는 함수
static void
usage(void) {
  printf(1, "usage: memstress [-n pages] [-t ticks] [-w]\n");
  exit();
}

int
main(int argc, char *argv[])
{
  // 기본값 설정
  int pages = 64;       // -n: 할당할 페이지 수 (기본 64)
  int hold_ticks = 200; // -t: 메모리를 유지할 시간 (기본 200 ticks)
  int do_write = 0;     // -w: 할당된 페이지에 쓰기 작업을 할지 여부 (기본 false)
  
  if(argc == 1){
    usage();  
  }

  // 1. 커맨드 라인 인자 파싱
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-n") == 0) {
      if (++i < argc) pages = atoi(argv[i]);
      else usage();
    } else if (strcmp(argv[i], "-t") == 0) {
      if (++i < argc) hold_ticks = atoi(argv[i]);
      else usage();
    } else if (strcmp(argv[i], "-w") == 0) {
      do_write = 1;
    } else {
      usage();
    }
  }

  int pid = getpid();
  printf(1, "[memstress] pid=%d pages=%d hold=%d ticks write=%d\n", pid, pages, hold_ticks, do_write);

  // 2. sbrk()를 호출하여 메모리 할당
  int inc = pages * 4096; 
  char *base = sbrk(inc);
  if (base == (char*)-1) {
    printf(1, "[memstress] sbrk failed\n");
    exit();
  }

  // 3. -w 옵션이 있으면, 각 페이지의 첫 바이트에 쓰기 작업 수행
  if (do_write) {
    for (int p = 0; p < pages; p++) {
      base[p*4096] = (char)(p & 0xff);
    }
  }

  // 4. 지정된 시간만큼 대기
  sleep(hold_ticks);

  printf(1, "[memstress] pid=%d done\n", pid);
  exit();
}

