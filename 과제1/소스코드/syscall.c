#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// ---------------------------------------------
// 시스템 콜 인자 처리 및 디스패치 구현부
// ---------------------------------------------

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then the first argument.

// ---------------- 인자 해석 함수 ----------------

// 유저 프로세스 주소 공간에서 int를 읽어온다.
int fetchint(uint addr, int *ip)
{
  struct proc *curproc = myproc();

  // 주소가 현재 프로세스 메모리 크기를 벗어나면 에러
  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  // 해당 주소에서 정수 값을 읽어옴
  *ip = *(int*)(addr);
  return 0;
}

// 유저 프로세스 주소 공간에서 문자열을 읽어온다.
// 실제 문자열을 복사하지 않고, 문자열 시작 주소를 *pp에 세팅.
// 리턴값은 문자열 길이 (널 문자는 제외).
int fetchstr(uint addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz) // 주소 범위 체크
    return -1;
  *pp = (char*)addr;       // 시작 주소 설정
  ep = (char*)curproc->sz; // 프로세스 메모리 끝 주소
  for(s = *pp; s < ep; s++){
    if(*s == 0)            // 문자열 끝(널 문자) 발견 시
      return s - *pp;      // 길이 반환
  }
  return -1;               // 끝을 찾지 못하면 에러
}

// n번째 시스템 콜 인자를 int로 가져옴
int argint(int n, int *ip)
{
  // esp(스택 포인터) + 4(리턴주소) + 4*n(n번째 인자)
  return fetchint((myproc()->tf->esp) + 4 + 4*n, ip);
}

// n번째 시스템 콜 인자를 포인터로 가져옴.
// size 크기만큼 유효한 주소인지 확인 후 *pp에 세팅.
int argptr(int n, char **pp, int size)
{
  int i;
  struct proc *curproc = myproc();
 
  if(argint(n, &i) < 0)   // 인자를 int로 읽음
    return -1;
  // 주소가 프로세스 메모리 범위 내에 있는지 확인
  if(size < 0 || (uint)i >= curproc->sz || (uint)i+size > curproc->sz)
    return -1;
  *pp = (char*)i;         // 포인터 인자로 반환
  return 0;
}

// n번째 시스템 콜 인자를 문자열로 가져옴.
// 문자열이 널로 종료되는지도 검사.
int argstr(int n, char **pp)
{
  int addr;
  if(argint(n, &addr) < 0)
    return -1;
  return fetchstr(addr, pp);
}

// ---------------- 시스템 콜 핸들러 extern 선언 ----------------
// 각 시스템 콜 구현 함수는 다른 파일(sysproc.c, sysfile.c 등)에 있음.
// 여기서는 extern으로 불러와서 디스패치 테이블(syscalls[])에 연결.

extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_kill(void);
extern int sys_link(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_uptime(void);

// 새로 추가한 시스템 콜
extern int sys_hello_number(void);
extern int sys_get_procinfo(void);

// ---------------- 시스템 콜 테이블 ----------------
// 인덱스 = 시스템 콜 번호
// 값 = 해당 시스템 콜 핸들러 함수 포인터
// 커널은 이 테이블을 참조해 올바른 함수를 실행함.

static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,    // (중복된 라인 있음: 하나 제거해도 무방)
[SYS_close]   sys_close,
[SYS_hello_number] sys_hello_number,   // 새로 추가한 hello_number
[SYS_get_procinfo] sys_get_procinfo    // 새로 추가한 get_procinfo
};

// ---------------- 시스템 콜 디스패처 ----------------
// 유저 프로그램이 int $T_SYSCALL 트랩을 걸면
// eax 레지스터에 들어 있는 시스템 콜 번호를 읽고,
// syscalls[] 테이블에서 해당 함수를 실행한다.

void syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax;  // eax = 시스템 콜 번호
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 유효한 번호일 경우 → 해당 시스템 콜 핸들러 실행
    curproc->tf->eax = syscalls[num]();  // 반환값은 eax에 저장
  } else {
    // 잘못된 번호면 에러 메시지 출력 후 -1 반환
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}

