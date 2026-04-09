// ---------------------------------------------
// 사용자 영역에서 참조하는 구조체와 시스템 콜 선언부
// (user.h 발췌)
// ---------------------------------------------

struct stat;    // 파일 상태 정보 구조체 (fstat에서 사용)
struct rtcdate; // 실시간 시계 정보 구조체 (date 관련)

// 프로세스 정보를 저장하는 구조체
// get_procinfo 시스템 콜을 통해 채워져 유저 영역으로 복사된다.
struct procinfo
{
  int pid;       // 프로세스 ID (자기 자신 또는 지정된 프로세스)
  int ppid;      // 부모 프로세스 ID
  int state;     // 프로세스 상태 (enum procstate 값: RUNNING, SLEEPING 등)
  uint sz;       // 프로세스 메모리 크기 (bytes 단위, uint는 types.h 정의)
  char name[16]; // 프로세스 이름 (xv6는 최대 16바이트 고정)
};

// ---------------------------------------------
// 시스템 콜 함수 원형 선언
// 사용자 프로그램은 이 선언을 통해 시스템 콜을 호출할 수 있다.
// 실제 구현은 커널 내부(sysproc.c, sysfile.c 등)에서 수행된다.
// ---------------------------------------------

int fork(void);                           // 새로운 프로세스 생성
int exit(void) __attribute__((noreturn)); // 현재 프로세스 종료
int wait(void);                           // 자식 프로세스 종료 대기
int pipe(int *);                          // 파이프 생성
int write(int, const void *, int);        // 파일/디스크립터에 쓰기
int read(int, void *, int);               // 파일/디스크립터에서 읽기
int close(int);                           // 파일 디스크립터 닫기
int kill(int);                            // 프로세스 강제 종료
int exec(char *, char **);                // 실행 파일 로드 및 실행
int open(const char *, int);              // 파일 열기
int mknod(const char *, short, short);    // 특수 파일 생성
int unlink(const char *);                 // 파일 삭제
int fstat(int fd, struct stat *);         // 파일 상태 정보 얻기
int link(const char *, const char *);     // 하드 링크 생성
int mkdir(const char *);                  // 디렉토리 생성
int chdir(const char *);                  // 작업 디렉토리 변경
int dup(int);                             // 파일 디스크립터 복제
int getpid(void);                         // 현재 프로세스 PID 반환
char *sbrk(int);                          // 힙 영역 크기 증가
int sleep(int);                           // 지정한 시간 동안 sleep
int uptime(void);                         // 부팅 이후 ticks 반환

// 새로 추가한 시스템 콜
int hello_number(int n); // 입력 정수를 출력 후 두 배 값을 반환
int get_procinfo(int pid, struct procinfo *uinfo);
// 지정된 PID 프로세스 정보를 uinfo에 채워 반환

// ---------------------------------------------
// 사용자 라이브러리 함수 원형 (ulib.c)
// xv6 사용자 프로그램에서 공용으로 사용하는 표준 라이브러리 성격
// ---------------------------------------------

int stat(const char *, struct stat *);    // 파일 상태 조회
char *strcpy(char *, const char *);       // 문자열 복사
void *memmove(void *, const void *, int); // 메모리 이동
char *strchr(const char *, char c);       // 문자열에서 문자 검색
int strcmp(const char *, const char *);   // 문자열 비교
void printf(int, const char *, ...);      // 콘솔 출력
char *gets(char *, int max);              // 문자열 입력 (표준입력)
uint strlen(const char *);                // 문자열 길이 계산
void *memset(void *, int, uint);          // 메모리 초기화
void *malloc(uint);                       // 동적 메모리 할당
void free(void *);                        // 동적 메모리 해제
int atoi(const char *);                   // 문자열을 정수로 변환
