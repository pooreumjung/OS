// helloxv6.c
// hello_number 시스템 콜을 테스트하기 위한 사용자 프로그램

#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  // 기본 테스트: 정수 5를 hello_number 시스템 콜에 전달
  // 커널 콘솔에는 "Hello, xv6! Your number is 5"가 출력되고
  // 반환값(= 5 * 2 = 10)이 res 변수에 저장된다.
  int res = hello_number(5);
  printf(1, "hello_number(5) returned %d\n", res);

  // 추가 테스트: 음수 입력도 전달 가능 (주석 해제 시 사용)
  // 예: -7을 전달하면 커널 콘솔에 "Hello, xv6! Your number is -7"이 출력되고
  // 반환값 -14가 사용자 프로그램에 전달된다.
  int res2 = hello_number(-7);
  printf(1, "hello_number(-7) returned %d\n", res2);

  // 프로그램 종료
  exit();
}
