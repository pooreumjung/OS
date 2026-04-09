#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h" 
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip = 0;
  struct proghdr ph;
  pde_t *pgdir = 0, *oldpgdir = 0; // oldpgdir 초기화
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail: namei path=%s\n", path);
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf)) {
    cprintf("exec: fail read elf header\n");
    goto bad;
  }
  if(elf.magic != ELF_MAGIC) {
    cprintf("exec: fail elf magic\n");
    goto bad;
  }

  if((pgdir = setupkvm()) == 0) { // 새 페이지 디렉터리 생성
    cprintf("exec: fail setupkvm\n");
    goto bad;
  }

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph)) {
      cprintf("exec: fail read proghdr\n");
      goto bad;
    }
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz) {
      cprintf("exec: fail memsz < filesz\n");
      goto bad;
    }
    if(ph.vaddr + ph.memsz < ph.vaddr) { // 주소 랩어라운드 체크
      cprintf("exec: fail vaddr wrap\n");
      goto bad;
    }
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0) { // 새 pgdir에 메모리 할당
      cprintf("exec: fail allocuvm sz\n");
      goto bad;
    }
    if(ph.vaddr % PGSIZE != 0) { // vaddr 정렬 체크
      cprintf("exec: fail vaddr align\n");
      goto bad;
    }
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0) { // 할당된 메모리에 ELF 섹션 로드
      cprintf("exec: fail loaduvm\n");
      goto bad;
    }
  }
  iunlockput(ip); // inode 사용 완료 후 unlock/put
  end_op();
  ip = 0; // ip 사용 완료 표시

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible (guard page). Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0) {
    cprintf("exec: fail allocuvm stack\n");
    goto bad;
  }
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE)); // 스택 아래 guard page 설정
  sp = sz; // 스택 포인터 초기화 (가장 높은 주소)

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG) {
      cprintf("exec: fail too many args\n");
      goto bad;
    }
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3; // 문자열 길이 + 널 종료 + 4바이트 정렬
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0) { // 스택에 인자 문자열 복사
      cprintf("exec: fail copyout arg string\n");
      goto bad;
    }
    ustack[3+argc] = sp; // 스택에 저장된 문자열 주소를 ustack 배열에 저장
  }
  ustack[3+argc] = 0; // argv 끝 표시 (null pointer)

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;        // argc
  ustack[2] = sp - (argc+1)*4;  // argv pointer (ustack 배열의 시작 주소)

  sp -= (3+argc+1) * 4; // ustack 배열 자체를 스택에 복사할 공간 확보
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0) { // ustack 배열을 스택에 복사
    cprintf("exec: fail copyout ustack\n");
    goto bad;
  }

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir; // 이전 페이지 디렉터리 백업
  curproc->pgdir = pgdir;   // 새 페이지 디렉터리로 교체
  curproc->sz = sz;         // 프로세스 크기 갱신
  curproc->tf->eip = elf.entry;  // 엔트리 포인트 설정 (main)
  curproc->tf->esp = sp;         // 스택 포인터 설정
  switchuvm(curproc);       // 새 페이지 테이블로 CR3 레지스터 교체

  // 이전 주소 공간 해제
  freevm(oldpgdir);

  // <-- (C-3/C-4) SW-TLB 무효화 추가 -->
  // freevm 후에 호출하여 이전 TLB 항목 정리
  if(curproc->pid > 0) {
    swtlb_invalidate_pid(curproc->pid); // <-- 이 호출이 필요합니다!
  }
  // <-- 추가 끝 -->

  return 0; // 성공

 bad:
  // 오류 발생 시 정리
  if(pgdir) // pgdir이 0이 아니면 freevm 호출
      freevm(pgdir);
      
  if(ip){ // ip가 아직 유효하면
    iunlockput(ip);
    end_op();
  }
  // cprintf("exec: fail bad\n"); // 상세 오류는 위에서 출력
  return -1;
}
