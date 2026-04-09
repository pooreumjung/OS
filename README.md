# OS

# 운영체제 설계과제 모음

xv6 기반 운영체제 과제 시리즈입니다. Linux 기초 실습부터 파일시스템 스냅샷 구현까지 총 5개의 과제로 구성되어 있습니다.

---

## 과제 목록

| 과제 | 제목                         | 주요 내용                                 |
| ---- | ---------------------------- | ----------------------------------------- |
| #0   | Linux 기본 명령어 실습       | `/proc` 파일시스템, 프로세스 모니터링     |
| #1   | xv6 설치 및 시스템 콜 추가   | `helloxv6`, `get_procinfo` 시스템 콜 구현 |
| #2   | Stride Scheduling            | xv6 커널에 Stride 스케줄러 구현           |
| #3   | Physical Page Frame Tracking | 물리 메모리 프레임 추적 테이블 구현       |
| #4   | Snapshot (Checkpointing)     | COW 기반 파일시스템 스냅샷 구현           |

---

## 과제 #0 — Linux 기본 명령어 실습

### 목표

Linux 환경에서 프로세스 모니터링 도구 및 `/proc` 파일시스템을 직접 실습하여 운영체제 기본 개념을 이해합니다.

### 주요 실습 내용

- **Linux 배포판 설치** : Ubuntu 20.x 이상 (Dual Boot / VirtualBox / AWS)
- **`/proc` 파일시스템 이해**
    - `/proc/cpuinfo` : CPU 개수, 클럭 주파수 확인
    - `/proc/meminfo` : 물리 메모리 크기, 여유 메모리 확인
    - `fork()` 이후 생성된 프로세스 수, context switch 횟수 확인
- **프로세스 모니터링** : `top`, `ps` 명령어로 실행 중인 프로세스 상태(CPU/메모리 사용률, PID, 상태) 확인
- **가상 메모리 vs 실제 메모리** : `memory1.c` / `memory2.c` 두 프로그램의 VSZ, RSS 비교
- **I/O 리디렉션 및 파이프 원리** : `fork()` + `exec()` + `fd` 조작을 통한 쉘 리디렉션 구현 원리 분석
- **디스크 I/O 모니터링** : `disk.c` / `disk1.c` 실행 중 `iostat`으로 디스크 사용률 측정

---

## 과제 #1 — xv6 설치 및 시스템 콜 추가

### 목표

MIT의 교육용 운영체제 xv6를 설치하고, 새로운 시스템 콜 두 개를 커널에 직접 추가합니다.

### 구현 내용

#### 1. xv6 설치 및 컴파일

```bash
git clone https://github.com/mit-pdos/xv6-public
apt-get install qemu-kvm
make
make qemu-nox
```

#### 2. `hello_number` 시스템 콜

정수 하나를 인자로 받아 커널 콘솔에 메시지를 출력하고, `n * 2`를 반환하는 시스템 콜입니다.

```c
// sysproc.c
int sys_hello_number(void) {
    int n;
    if (argint(0, &n) < 0) return -1;
    cprintf("Hello, xv6! Your number is %d\n", n);
    return n * 2;
}
```

수정 파일 : `syscall.h`, `syscall.c`, `sysproc.c`, `usys.S`, `user.h`

#### 3. `get_procinfo` 시스템 콜

PID를 입력받아 해당 프로세스의 정보(PID, PPID, 상태, 메모리 크기, 이름)를 구조체로 반환합니다.

```c
int get_procinfo(int pid, struct procinfo *uinfo);
// pid > 0 : 해당 PID 프로세스 조회
// pid <= 0 : 호출한 프로세스 자기 자신 조회
```

#### 4. 사용자 프로그램

- `helloxv6.c` : `hello_number()` 시스템 콜 테스트
- `psinfo.c` : `get_procinfo()`로 프로세스 정보 출력

```
$ helloxv6
Hello, xv6! Your number is 5
hello_number(5) returned 10

$ psinfo
PID=4 PPID=2 STATE=RUNNING SZ=12288 NAME=psinfo
```

---

## 과제 #2 — Stride Scheduling in xv6

### 목표

티켓 수에 비례하여 CPU 시간을 결정론적으로 분배하는 Stride 스케줄러를 xv6 커널에 구현합니다.

### 배경 지식

| 방식        | 특징                                               |
| ----------- | -------------------------------------------------- |
| Round Robin | 모든 프로세스에 동일한 CPU 시간 부여               |
| Lottery     | 확률적 추첨으로 CPU 할당, 단기 변동성 존재         |
| **Stride**  | `pass` 값 기반 결정론적 선택, 예측 가능하고 안정적 |

Stride 스케줄링 핵심 : `stride = STRIDE_MAX / tickets`, 매 틱마다 `pass += stride`, 항상 `pass`가 가장 작은 프로세스를 선택합니다.

### 구현 내용

#### 1. `proc` 구조체 확장 (`proc.h`)

```c
struct proc {
    // ... 기존 필드 ...
    int tickets;
    uint stride;       // 기본값 0 (settickets으로 설정)
    uint pass;         // 기본값 0
    int ticks;         // 기본값 0
    int end_ticks;     // 기본값 -1 (양수면 해당 틱에 프로세스 종료)
};

#define STRIDE_MAX   100000
#define PASS_MAX      15000
#define DISTANCE_MAX   7500
```

#### 2. `settickets` 시스템 콜 (번호: 22)

```c
int settickets(int tickets, int end_ticks);
// tickets : 1 이상 STRIDE_MAX 이하
// end_ticks : 1 이상이면 프로세스 수명 설정, 아니면 무시
```

#### 3. Stride 스케줄러 (`scheduler()` 함수 수정)

- `RUNNABLE` 프로세스 중 `pass`가 가장 작은 프로세스 선택 (동률이면 `pid`가 작은 것 우선)
- 1틱 실행 후 `pass += stride` 갱신
- **오버플로우 방지** : `pass > PASS_MAX`가 되면 모든 `RUNNABLE` 프로세스의 `pass`를 최솟값만큼 감소시키고, 프로세스 간 `pass` 차이가 `DISTANCE_MAX`를 넘지 않도록 조정

#### 4. 실행 예시

```
$ syscall_test
Process4 selected, stride:1000, ticket:100, pass:0 -> 1000 (1/6)
Process4 selected, stride:1000, ticket:100, pass:1000 -> 2000 (2/6)
...
Process4 exit
```

---

## 과제 #3 — Physical Page Frame Tracking in xv6

### 목표

xv6 커널의 메모리 할당기(`kalloc`/`kfree`)를 확장하여 모든 물리 메모리 프레임의 실시간 사용 현황을 추적합니다.

### 구현 내용

#### A. 전역 프레임 정보 테이블 (필수)

모든 물리 페이지 프레임에 대해 아래 정보를 저장하는 전역 테이블을 커널에 유지합니다.

```c
struct physframe_info {
    uint frame_index;   // 물리 프레임 번호
    int  allocated;     // 1: 사용 중, 0: free
    int  pid;           // 소유 프로세스 PID (-1이면 미사용)
    uint start_tick;    // 현재 PID가 사용 시작한 시각 (tick)
};

#define PFNNUM 60000
struct physframe_info pf_info[PFNNUM];
```

#### `kalloc` / `kfree` 수정

- **`kalloc()`** : 페이지 할당 시 해당 프레임 엔트리에 `allocated = 1`, `pid = 현재 PID`, `start_tick = ticks` 기록
- **`kfree()`** : 페이지 해제 시 `allocated = 0`, `pid = -1`, `start_tick = 0` 초기화 (free list 반환 직전, `kmem.lock` 보유 상태에서 수행)

#### `dump_physmem_info` 시스템 콜

```c
int dump_physmem_info(void *addr, int max_entries);
// 커널 전역 테이블에서 max_entries개까지 읽어 유저 버퍼에 복사 (copyout 사용)
// 복사된 엔트리 개수 반환
```

#### B. 테스트 프로그램

- `memstress` : `-n`, `-t`, `-w` 옵션으로 페이지 할당/유지/쓰기 수행
- `memdump` : `dump_physmem_info()`로 받은 프레임 정보를 표 형태로 출력
    - 기본: 할당된 프레임만 출력
    - `-a` : 전체 프레임 출력
    - `-p <PID>` : 특정 PID 프레임만 필터
- `memtest` : 위 두 프로그램을 조합한 통합 테스트

#### C. 추가 기능

- **`sw_vtop()`** : 소프트웨어로 가상주소 → 물리주소 변환 (PDE/PTE 직접 파싱)
- **역페이지 테이블(IPT)** : 물리페이지 → (프로세스, 가상주소) 역방향 매핑 구현
- **소프트웨어 TLB** : 최근 N개 `(pid, va) → pa` 캐시, 히트율 측정

---

## 과제 #4 — Snapshot (Checkpointing) in xv6

### 목표

Copy-On-Write(COW) 기법을 활용하여 xv6 파일시스템에 스냅샷 기능을 추가합니다. Oracle ZFS 등 실제 파일시스템과 유사한 공간 효율적 백업/복구 기능을 구현합니다.

### 구현 내용

#### A. Block COW 구현

스냅샷 생성 시 전체 데이터를 복사하지 않고, 기존 블록을 공유 참조합니다. 수정이 발생할 때만 새 블록을 할당합니다.

- `/snapshot` 디렉토리 아래에 블록별 참조 횟수를 저장하는 메타데이터 파일 유지
- 스냅샷이 참조하는 블록 → **불변 블록**으로 취급
- 현재 파일시스템에서 불변 블록을 수정하려 하면 → **COW 적용** (새 블록 할당 후 복사)
- 모든 참조가 사라진 블록 → 즉시 해제

#### B. 스냅샷 시스템 콜

```c
int snapshot_create(void);
// 현재 파일시스템 상태를 스냅샷으로 저장
// /snapshot/[ID]/ 디렉토리에 현재 루트의 모든 파일/폴더를 재귀적으로 캡처
// 성공 시 스냅샷 ID 반환, 실패 시 음수 반환

int snapshot_rollback(int snap_id);
// 현재 파일시스템을 지정한 ID의 스냅샷으로 복구
// 실패 시 음수 반환

int snapshot_delete(int snap_id);
// 지정한 ID의 스냅샷 삭제
// 실패 시 음수 반환
```

#### C. 테스트 프로그램

- `mk_test_file` : 테스트용 파일 생성
- `append` : 파일 끝에 문자열 추가
- `print_addr` : 파일이 참조하는 블록 주소 출력
- `snap_create` / `snap_rollback` / `snap_delete` : 각 시스템 콜 테스트용 쉘 프로그램

```
$ snap_create
$ ls /snapshot/01/
README  cat  echo  sh  ...

$ print_addr README
addr[0]: 3c
addr[1]: 3d
...
```

---

## 개발 환경

- **OS** : Ubuntu 20.04 LTS 이상
- **에뮬레이터** : QEMU (`qemu-kvm`)
- **대상 OS** : [xv6-public](https://github.com/mit-pdos/xv6-public) (x86)
- **컴파일러** : GCC (Cross Compile — 호스트 Linux에서 빌드 후 xv6에서 실행)

```bash
# xv6 빌드 및 실행
git clone https://github.com/mit-pdos/xv6-public
cd xv6-public
make
make qemu-nox
```

---

## 디렉토리 구조

```
.
├── assignment0/          # Linux 기본 명령어 실습 보고서
├── assignment1/          # xv6 시스템 콜 추가
│   ├── xv6-public/       # 수정된 xv6 소스
│   ├── helloxv6.c
│   └── psinfo.c
├── assignment2/          # Stride 스케줄러
│   └── xv6-public/
├── assignment3/          # 물리 메모리 프레임 추적
│   └── xv6-public/
└── assignment4/          # 파일시스템 스냅샷
    └── xv6-public/
```

> 각 과제는 독립적인 xv6 소스를 기반으로 합니다. 이전 과제의 코드가 다음 과제에 포함되지 않습니다.
