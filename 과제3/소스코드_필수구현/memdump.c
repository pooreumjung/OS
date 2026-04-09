#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define PFNNUM 60000 

// 잘못된 사용법을 알려주는 함수
static void
usage(void)
{
    printf(1, "usage: memdump [-a] [-p PID]\n");
    exit();
}

int main(int argc, char *argv[])
{
    // 옵션 처리를 위한 변수
    int show_all = 0;
    int filter_pid = -1;
    
   // 인자가 아무것도 없는 경우 (xv6는 argv[0]="memdump"만 존재)
    if (argc == 1) {
     usage();
    }

    // 1. 커맨드 라인 인자 파싱
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            if (++i < argc) {
                filter_pid = atoi(argv[i]);
            } else {
                usage();
            }
        } else {
            usage();
        }
    }
    
    // 2. 커널로부터 프레임 정보를 받아올 버퍼 선언
    // malloc 대신 정적 배열을 사용해도 무방합니다.
    static struct physframe_info buf[PFNNUM];
    int n = dump_physmem_info((void *)buf, PFNNUM);
    if (n < 0)
    {
        printf(1, "memdump: dump_physmem_info failed\n");
        exit();
    }

    // 3. 결과 출력
    printf(1, "[memdump] pid=%d\n", getpid());
    printf(1, "[frame#]\t[alloc]\t[pid]\t[start_tick]\n");

    for (int i = 0; i < n; i++) {
        struct physframe_info *pf = &buf[i];

        // -a 옵션 처리
        if (show_all) {
            printf(1, "%d\t\t%d\t%d\t%d\n", pf->frame_index, pf->allocated, pf->pid, pf->start_tick);
            continue;
        }

        // -p 옵션 처리
        if (filter_pid != -1) {
            if (pf->pid == filter_pid) {
                printf(1, "%d\t\t%d\t%d\t%d\n", pf->frame_index, pf->allocated, pf->pid, pf->start_tick);
            }
            continue;
        }

        // 기본 동작: 할당된 프레임만 출력
        if (pf->allocated) {
            printf(1, "%d\t\t%d\t%d\t%d\n", pf->frame_index, pf->allocated, pf->pid, pf->start_tick);
        }
    }
    
    exit();
}

