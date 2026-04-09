// defs.h
struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct rtcdate;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

// bio.c
void            binit(void);
struct buf* bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);

// console.c
void            consoleinit(void);
void            cprintf(char*, ...);
void            consoleintr(int(*)(void));
void            panic(char*) __attribute__((noreturn));

// exec.c
int             exec(char*, char**);

// file.c
struct file* filealloc(void);
void            fileclose(struct file*);
struct file* filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, char*, int n);
int             filestat(struct file*, struct stat*);
int             filewrite(struct file*, char*, int n);

// fs.c
void            readsb(int dev, struct superblock *sb);
int             dirlink(struct inode*, char*, uint);
struct inode* dirlookup(struct inode*, char*, uint*);
struct inode* ialloc(uint, short);
struct inode* idup(struct inode*);
void            iinit(int dev);
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode* namei(char*);
struct inode* nameiparent(char*, char*);
int             readi(struct inode*, char*, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, char*, uint, uint);

// ide.c
void            ideinit(void);
void            ideintr(void);
void            iderw(struct buf*);

// ioapic.c
void            ioapicenable(int irq, int cpu);
extern uchar    ioapicid;
void            ioapicinit(void);

// kalloc.c
char* kalloc(void);
void            kfree(char*);
void            kinit1(void*, void*);
void            kinit2(void*, void*);
uint    get_refcount(uint pa);
int     decr_refcount(uint pa);
void    incr_refcount(uint pa);

// kbd.c
void            kbdintr(void);

// lapic.c
void            cmostime(struct rtcdate *r);
int             lapicid(void);
extern volatile uint* lapic;
void            lapiceoi(void);
void            lapicinit(void);
void            lapicstartap(uchar, uint);
void            microdelay(int); 

// log.c
void            begin_op(void);
void            end_op(void);
void            log_write(struct buf *);
void            initlog(int dev);

// mp.c
extern int      ismp;
void            mpinit(void);

// picirq.c
void            picenable(int);
void            picinit(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, char*, int);
int             pipewrite(struct pipe*, char*, int);

// proc.c
int             cpuid(void);
void            exit(void);
int             fork(void);
int             growproc(int);
int             kill(int);
struct cpu* mycpu(void);
struct proc* myproc();
void            pinit(void);
void            procdump(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            setproc(struct proc*);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(void);
void            wakeup(void*);
void            yield(void);

// swtch.S
void            swtch(struct context**, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
void            getcallerpcs(void*, uint*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            pushcli(void);
void            popcli(void);

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);

// string.c
int             memcmp(const void*, const void*, uint);
void* memmove(void*, const void*, uint);
void* memset(void*, int, uint);
char* safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char* strncpy(char*, const char*, int);

// syscall.c
int             argint(int, int*);
int             argptr(int, char**, int);
int             argstr(int, char**);
int             fetchint(uint, int*);
int             fetchstr(uint, char**);
void            syscall(void);

// timer.c
void            timerinit(void);

// trap.c
void            idtinit(void);
extern uint     ticks;
void            tvinit(void);
extern struct spinlock tickslock;

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartputc(int);

// vm.c
void            seginit(void);
void            kvmalloc(void);
pde_t* setupkvm(void);
char* uva2ka(pde_t*, char*);
int             allocuvm(pde_t*, uint, uint);
int             deallocuvm(pde_t*, uint, uint);
void            freevm(pde_t*);
void            inituvm(pde_t*, char*, uint);
int             loaduvm(pde_t*, char*, struct inode*, uint, uint);
pde_t* copyuvm(pde_t *pgdir, uint sz, struct proc *child);
void            switchuvm(struct proc*);
void            switchkvm(void);
int             copyout(pde_t*, uint, void*, uint);
void            clearpteu(pde_t *pgdir, char *uva);
uint* walkpgdir(pde_t *pgdir, const void *va, int alloc);

#define IPT_BUCKETS     4096
#define MAX_IPT_ENTRIES 60000
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
#define PFNNUM 60000 // 물리 프레임 번호 개수

// 물리 프레임 정보를 담는 구조체
struct physframe_info {
  uint frame_index;   // 물리 프레임 번호
  int allocated;      // 1이면 할당됨, 0이면 free 상태
  int pid;            // 소유 프로세스 PID (없으면 -1)
  uint start_tick;    // 이 프레임을 현재 PID가 사용 시작한 시각 (tick)
};

extern struct physframe_info pftable[PFNNUM]; 
extern int pfn_refcnt[PFNNUM];

struct vlist {
	uint pid;
	uint va_page;
	uint flags;
};

// ----------------------------------------------------------------------
// (C-5) Inverted Page Table (IPT)
//  - 각 물리 프레임 번호(PFN)에 대응되는 (pid, va, flags) 매핑 정보 저장
//  - Copy-on-Write, page tracing, phys2virt 등에 사용됨
// ----------------------------------------------------------------------

struct ipt_entry {
  uint32_t pfn;             // 물리 프레임 번호 (Page Frame Number)
  uint32_t pid;             // 소유 프로세스 PID
  uint32_t va;              // 매핑된 가상주소 (페이지 단위)
  uint16_t flags;           // 페이지 권한 비트 (PTE_P, PTE_W 등)
  uint16_t refcnt;          // 디버깅용 참조 카운트
  struct ipt_entry *next;   // 해시 체인 포인터
};

// ipt.c
void    ipt_init(void);
int     ipt_insert(uint va, uint pa, uint pid, uint flag);
int     ipt_remove(uint va, uint pa, uint pid);
void    ipt_remove_pid(uint pid);
int     ipt_find(uint pfn, struct vlist *out_buffer, int max_entries);
int     ipt_update_flags(uint pid, uint va, uint16_t newflags);  

// swtlb.c (C-3)
void    swtlb_init(void);
int     swtlb_lookup(uint pid, uint va, uint *pa_out, uint *flags_out);
void    swtlb_insert(uint pid, uint va, uint pa, uint flags);
void    swtlb_invalidate_pid(uint pid);
void    swtlb_invalidate(uint pid, uint va);
void    swtlb_get_stats(uint *hits_out, uint *misses_out);

