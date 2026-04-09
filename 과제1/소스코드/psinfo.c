// psinfo.c
// get_procinfo 시스템 콜을 테스트하기 위한 사용자 프로그램
// 주어진 PID에 해당하는 프로세스 정보를 커널에서 받아와 출력한다.

#include "types.h"
#include "stat.h"
#include "user.h"

// 커널 enum procstate의 값(0~5)을 문자열로 변환하는 함수
// -> 프로세스 상태를 사람이 읽기 쉬운 형태로 출력하기 위해 사용
static char* s2str(int s){
  switch(s){
    case 0: return "UNUSED";    // 아직 사용되지 않은 엔트리
    case 1: return "EMBRYO";    // 생성 중인 상태
    case 2: return "SLEEPING";  // 대기(수면) 상태
    case 3: return "RUNNABLE";  // 실행 가능 상태
    case 4: return "RUNNING";   // 실제 실행 중
    case 5: return "ZOMBIE";    // 종료됐지만 부모가 수거하지 않은 상태
  }
  return "UNKNOWN"; // 알 수 없는 값 (예외 처리)
}

int main(int argc, char *argv[]){
  struct procinfo info; // 커널에서 전달받을 프로세스 정보 구조체
  int pid = 0;          // 기본값: 0 → 자기 자신 프로세스 정보 요청
  if(argc >= 2) pid = atoi(argv[1]); // 인자가 있으면 해당 PID로 조회

  // get_procinfo(pid, &info)를 호출하여 커널에 정보 요청
  // 리턴값이 < 0이면 실패한 것임 (존재하지 않는 PID 등)
  if(get_procinfo(pid, &info) < 0){
    printf(1, "psinfo: failed to get procinfo (pid=%d)\n", pid);
    exit();
  }

  // 성공 시 커널에서 채워준 procinfo 구조체의 값들을 출력
  // PID, 부모 PID, 상태, 메모리 크기, 프로세스 이름
  printf(1, "PID=%d PPID=%d STATE=%s SZ=%d NAME=%s\n",
         info.pid, info.ppid, s2str(info.state), info.sz, info.name);

  // 프로그램 종료
  exit();
}

