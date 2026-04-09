#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  int fd;

  if(argc < 2){
    printf(1, "need argv[1]\n");
    exit();
  }

  // argv[1] 파일을 새로 생성하여 쓰기 전용으로 연다.
  if((fd = open(argv[1], O_CREATE | O_WRONLY)) < 0){
    printf(1, "open error for %s\n", argv[0]);
    exit();
  }

  char buf[513];

  // 512바이트 채우기 위한 버퍼 초기화
  for(int i = 1; i < 511; i++) buf[i] = 0;
  buf[511] = '\n';  // 마지막은 줄바꿈

  /*
   * 12개의 direct block을 각기 다른 숫자(0~11)로 시작하는 데이터로 채움
   * → print_addr 로 block 번호가 정확히 찍히는지 관찰하기 위해 사용
   */
  for(int i = 0; i < 12; i++){
    buf[0] = (i % 10) + '0';   // 첫 바이트 = '0'~'9'
    write(fd, buf, 512);      // 512 bytes = 1 block
  }

  // direct block 모두 채운 뒤 문자열 기록 → 이후 append 동작 실험 가능
  char *str = "hello\n";
  write(fd, str, 6);

  close(fd);
  exit();
}

