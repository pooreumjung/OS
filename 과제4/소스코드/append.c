#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    printf(2, "Usage: append filename string\n");
    exit();
  }

  // O_RDWR로 열고, 없으면 생성
  int fd = open(argv[1], O_RDWR | O_CREATE);
  if(fd < 0){
    printf(2, "append: cannot open %s\n", argv[1]);
    exit();
  }

  char buf[1];

  /*
   * 파일의 끝으로 이동하는 방식:
   *   read() 를 1바이트씩 호출하여 EOF까지 이동
   * (seek 기능이 없는 xv6 특성을 고려한 방식)
   */
  while(read(fd, buf, 1) == 1);

  /*
   * 문자열 append
   * 스냅샷 경로(/snapshot/XX/...)라면 커널에서 write-block이 막혀야 함
   * → append: write failed 출력됨
   */
  if(write(fd, argv[2], strlen(argv[2])) < 0){
    printf(2, "append: write failed\n");
    close(fd);
    exit();
  }

  close(fd);
  exit();
}

