struct stat;
struct rtcdate;


// 커널과 사용자 공간이 동일한 구조체 정의를 공유합니다. 
struct physframe_info {
  uint frame_index;
  int allocated;      // 1이면 할당됨, 0이면 free 상태
  int pid;            // 소유 프로세스 PID
  uint start_tick;    // 할당 시작 시점
};

struct vlist {
  uint32_t pid;
  uint32_t va_page;
  uint16_t flags;
};

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int dump_physmem_info(void *addr, int max_entries);
int vtop(void *va, uint *pa_out, uint *flags_out);
int fprintf(int, const char*, ...);
int phys2virt(uint32_t pa_page, struct vlist *out, int max);
int get_tlb_stats(uint *hits, uint *misses);
