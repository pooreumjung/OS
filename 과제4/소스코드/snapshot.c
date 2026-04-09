// ============================================================
// snapshot.c — FINAL STABLE VERSION (Commented)
// 1. Fixed MISSING LOCKS in copy_tree/restore_tree/clone_file
//    (Critical: readi/writei must hold locks!)
// 2. Includes Debug logs & Transaction safety
// 3. Includes Boot-time Refcnt Recovery
// ============================================================

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

// fs.c에 정의된 블록 참조 카운트 배열 (전역 변수)
extern int block_refcnt[20000]; 
// 참조 카운트 초기화 여부를 확인하는 플래그
static int refcnt_initialized = 0;

// ============================================================
// Utilities (문자열 및 트랜잭션 헬퍼 함수)
// ============================================================

// 문자열 비교 함수
static int xstrcmp(const char *s, const char *t) {
  while (*s && *s == *t) { s++; t++; }
  return (uchar)*s - (uchar)*t;
}

// 정수를 문자열로 변환 (스냅샷 ID 포맷팅용)
static void xitoa(int n, char *buf) {
  int i=0; char tmp[16];
  if(n==0){ buf[0]='0'; buf[1]=0; return; }
  while(n){ tmp[i++]='0'+(n%10); n/=10; }
  int k=0; while(i) buf[k++]=tmp[--i];
  buf[k]=0;
}

// 문자열 이어붙이기
static void xstrcat(char *dst, const char *src) {
  while(*dst) dst++;
  while((*dst++ = *src++));
}

// 문자열 일치 여부 확인 (길이까지 완벽하게 비교하여 부분 일치 방지)
static int name_equal(const char *s, const char *t) {
  int i;
  for(i = 0; s[i] && t[i]; i++){
    if(s[i] != t[i]) return 0;
  }
  // 두 문자열이 동시에 끝나야만 같은 문자열로 간주
  if(s[i] == 0 && t[i] == 0) return 1;
  return 0;
}

// 스냅샷 이름 생성 (예: 1 -> "01")
static void format_snap_name(int id, char *out) {
  if(id < 10){
    out[0]='0'; out[1]='0'+id; out[2]=0;
  } else xitoa(id, out);
}

// 트랜잭션 분할 함수: 로그 오버플로우를 방지하기 위해 현재 트랜잭션을 닫고 새로 엶
static inline void bump_txn() {
  end_op();
  begin_op();
}

// ============================================================
// Init Refcnt (부팅 시 참조 카운트 복구 - Double Free 방지)
// ============================================================
static void ensure_refcnt_init(void) {
  if (refcnt_initialized) return; // 이미 초기화되었으면 건너뜀

  struct superblock sb;
  readsb(ROOTDEV, &sb); 

  // 디스크의 모든 Inode를 스캔하여 사용 중인 블록의 참조 카운트를 1로 설정
  for (int inum = 1; inum < sb.ninodes; inum++) {
    begin_op(); // iput 등의 연산이 디스크 쓰기를 유발할 수 있으므로 트랜잭션 시작
    struct inode *ip = iget(ROOTDEV, inum);
    ilock(ip); // Inode 정보를 메모리로 읽어오기 위해 잠금 필수

    if (ip->type == 0) { // 사용하지 않는 Inode는 패스
      iunlock(ip);
      iput(ip);
      end_op(); 
      continue;
    }

    // 1. Direct Block(직접 블록) 참조 카운트 복구
    for (int i = 0; i < NDIRECT; i++) {
      uint bn = ip->addrs[i];
      if (bn != 0 && block_refcnt[bn] == 0) block_refcnt[bn] = 1;
    }

    // 2. Indirect Block(간접 블록) 참조 카운트 복구
    uint bind = ip->addrs[NDIRECT];
    if (bind != 0) {
      // 간접 블록 자체의 카운트
      if(block_refcnt[bind] == 0) block_refcnt[bind] = 1;
      
      // 간접 블록이 가리키는 데이터 블록들의 카운트
      struct buf *bp = bread(ROOTDEV, bind);
      uint *ap = (uint*)bp->data;
      for (int j = 0; j < NINDIRECT; j++) {
        uint bn = ap[j];
        if (bn != 0 && block_refcnt[bn] == 0) block_refcnt[bn] = 1;
      }
      brelse(bp);
    }

    iunlock(ip);
    iput(ip);
    end_op(); 
  }
  refcnt_initialized = 1; // 초기화 완료 표시
}

// ============================================================
// Core Logic (핵심 로직)
// ============================================================

// 빈 디렉토리 생성 함수 (., .. 포함)
static struct inode* make_empty_dir(struct inode *parent, char *name)
{
  begin_op();
  ilock(parent); // [중요] dirlink를 호출하려면 부모 디렉토리가 잠겨 있어야 함

  struct inode *ip = ialloc(parent->dev, T_DIR);
  if(!ip){
    iunlock(parent);
    end_op();
    return 0;
  }

  ilock(ip);
  ip->nlink = 2; // 디렉토리의 기본 링크 수 (자기 자신, 부모)
  iupdate(ip);

  // "."과 ".." 엔트리 생성
  dirlink(ip, ".",  ip->inum);
  dirlink(ip, "..", parent->inum);
  iunlock(ip);

  // 부모 디렉토리에 새 디렉토리 연결
  if(dirlink(parent, name, ip->inum) < 0){
    iunlock(parent);
    iput(ip);
    end_op();
    return 0;
  }

  parent->nlink++; // 하위 디렉토리 생성 시 부모의 링크 수 증가
  iupdate(parent);
  iunlock(parent);
  end_op();

  return ip;
}

// 파일 복제 함수 (COW: Copy-On-Write 적용)
// src: 원본 파일, dst_dir: 복제될 위치, is_snap: 스냅샷 여부
static struct inode*
clone_file(struct inode *src, struct inode *dst_dir, char *name, int is_snap)
{
  begin_op();

  // 새 Inode 할당
  struct inode *dst = ialloc(src->dev, T_FILE);
  if (!dst) {
    end_op();
    return 0;
  }

  ilock(dst);
  dst->nlink = 1;
  dst->size  = src->size;

  // 1. Direct Block 복사 (블록 번호만 복사하고 참조 카운트 증가 -> 데이터 공유)
  for (int i = 0; i < NDIRECT; i++) {
    dst->addrs[i] = src->addrs[i];
    if (src->addrs[i])
      block_refcnt[src->addrs[i]]++;
  }

  // 2. Indirect Block 처리
  if (src->addrs[NDIRECT]) {
    uint old_ind = src->addrs[NDIRECT];
    uint new_ind = balloc(src->dev); // 간접 블록 자체는 새로 할당
    
    // 새로 할당된 블록의 참조 카운트 안전장치
    if(block_refcnt[new_ind] == 0) block_refcnt[new_ind] = 1;

    dst->addrs[NDIRECT] = new_ind;

    struct buf *bp_old = bread(src->dev, old_ind);
    struct buf *bp_new = bread(src->dev, new_ind);
    uint *a_old = (uint*)bp_old->data;
    uint *a_new = (uint*)bp_new->data;

    // 내부 데이터 블록들은 공유 (참조 카운트 증가)
    for (int j = 0; j < NINDIRECT; j++) {
      a_new[j] = a_old[j];
      if (a_old[j])
        block_refcnt[a_old[j]]++; 
    }
    log_write(bp_new);
    brelse(bp_old);
    brelse(bp_new);
  } else {
    dst->addrs[NDIRECT] = 0;
  }

  // 스냅샷 생성 시에는 I_SNAP 플래그 추가 (수정 불가)
  if(is_snap)
    dst->flags |= I_SNAP;
  
  iupdate(dst);
  iunlock(dst);

  // [중요 수정] dirlink 호출 전 타겟 디렉토리를 반드시 잠가야 함
  ilock(dst_dir);
  if (dirlink(dst_dir, name, dst->inum) < 0) {
    iunlock(dst_dir);
    iput(dst);
    end_op();
    return 0;
  }
  iunlock(dst_dir);

  iput(dst);
  end_op();
  return dst;
}

// [DEBUG] 디렉토리 내용 삭제 함수 (로그 출력 포함)
static void wipe_dir_content(struct inode *dir)
{
  struct dirent de;
  struct dirent empty;
  memset(&empty, 0, sizeof(empty));

  // 디렉토리 엔트리 순회 (offset 0, 1은 ., .. 이므로 건너뜀)
  for (uint off = 2*sizeof(de); off < dir->size; off += sizeof(de)) {
    if (readi(dir, (char*)&de, off, sizeof(de)) != sizeof(de)) continue;
    if (de.inum == 0) continue;

    // 스냅샷 디렉토리는 지우지 않음
    if (name_equal(de.name, "snapshot")) {
      continue;
    }

    // 해당 파일/디렉토리의 Inode 링크 수 감소 (실제 삭제 유도)
    struct inode *child = iget(dir->dev, de.inum);
    ilock(child);
    if(child->nlink > 0) {
      child->nlink--; 
      iupdate(child);
    }
    iunlock(child);
    iput(child);

    // 디렉토리 엔트리를 0으로 덮어씀 (목록에서 삭제)
    writei(dir, (char*)&empty, off, sizeof(empty));
    
    // [중요] 파일 하나 처리할 때마다 트랜잭션 갱신 (Log Overflow 방지)
    bump_txn(); 
  }
}

// 재귀적 트리 복사 함수 (스냅샷 생성용)
static void copy_tree(struct inode *src, struct inode *dst)
{
  struct dirent de;
  char name[DIRSIZ];
  
  // [중요] readi를 사용하려면 src 디렉토리가 잠겨 있어야 함
  ilock(src);

  for(uint off=0; off < src->size; off += sizeof(de)){
    if(readi(src, (char*)&de, off, sizeof(de)) != sizeof(de)) continue;
    if(de.inum == 0) continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ-1] = 0;

    if(name_equal(name, ".") || name_equal(name, "..")) continue;
    if(name_equal(name, "snapshot")) continue;

    // 자식 노드 처리를 위해 잠시 src 잠금 해제 (데드락 방지)
    iunlock(src);

    struct inode *child = iget(src->dev, de.inum);
    ilock(child);

    if(child->type == T_DIR){
      // 디렉토리면 재귀 호출
      struct inode *newdir = make_empty_dir(dst, name);
      iunlock(child);
      copy_tree(child, newdir);
      iput(newdir);
    } else {
      // 파일이면 clone_file (COW 복사)
      // dst는 clone_file 내부에서 잠금 처리됨
      clone_file(child, dst, name, 1); 
      iunlock(child);
    }
    iput(child);
    bump_txn(); // 트랜잭션 분할

    // 다음 반복을 위해 src 다시 잠금
    ilock(src);
  }
  iunlock(src);
}

// 트리 복구 함수 (롤백용)
static void restore_tree(struct inode *snapdir, struct inode *root)
{
  struct dirent de;
  char name[DIRSIZ];

  // [중요] 스냅샷 디렉토리 읽기를 위해 잠금
  ilock(snapdir);

  for(uint off=0; off < snapdir->size; off += sizeof(de)){
    if(readi(snapdir, (char*)&de, off, sizeof(de)) != sizeof(de)) continue;
    if(de.inum == 0) continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ-1] = 0;

    if(name_equal(name, ".") || name_equal(name, "..")) continue;
    if(name_equal(name, "snapshot")) continue;

    iunlock(snapdir); // 자식 처리를 위해 잠금 해제

    struct inode *child = iget(snapdir->dev, de.inum);
    ilock(child);

    if(child->type == T_DIR){
      struct inode *newdir = make_empty_dir(root, name);
      iunlock(child);
      restore_tree(child, newdir);
      iput(newdir);
    } else {
      // 복구 시에는 수정 가능한 파일로 생성 (is_snap = 0)
      clone_file(child, root, name, 0); 
      iunlock(child);
    }
    
    iput(child);
    bump_txn();

    ilock(snapdir); // 다시 잠금
  }
  iunlock(snapdir);
}

// ============================================================
// System Calls (시스템 콜 구현부)
// ============================================================

int snapshot_create(void)
{
  ensure_refcnt_init(); // 참조 카운트 안전장치 실행

  struct inode *snaproot = namei("/snapshot");
  if(!snaproot) return -1;

  static int next_id = 1;
  int id = next_id++;

  char name[16];
  format_snap_name(id, name);

  // /snapshot/01 디렉토리 생성
  struct inode *newdir = make_empty_dir(snaproot, name);
  if(!newdir){
    iput(snaproot);
    return -1;
  }

  struct inode *root = namei("/");
  if(!root){
    iput(newdir);
    iput(snaproot);
    return -1;
  }

  // 루트 디렉토리의 내용을 스냅샷 디렉토리로 복사
  copy_tree(root, newdir);

  iput(root);
  iput(newdir);
  iput(snaproot);
  return id;
}

int snapshot_rollback(int snap_id)
{
  ensure_refcnt_init();

  char name[16];
  format_snap_name(snap_id, name);

  char path[32]="/snapshot/";
  xstrcat(path, name);

  struct inode *snapdir = namei(path);
  if(!snapdir) return -1;

  struct inode *root = namei("/");
  if(!root){
    iput(snapdir);
    return -1;
  }

  // 1. 현재 루트 디렉토리 비우기 (트랜잭션 분할 적용)
  begin_op();
  ilock(root);
  wipe_dir_content(root); 
  iunlock(root);
  end_op();

  // 2. 스냅샷 내용으로 루트 복구
  restore_tree(snapdir, root);

  iput(root);
  iput(snapdir);
  return 0;
}

int snapshot_delete(int snap_id)
{
  char name[16];
  format_snap_name(snap_id, name);

  char path[32]="/snapshot/";
  xstrcat(path, name);

  struct inode *dir = namei(path);
  if(!dir) return -1;

  // 1. 해당 스냅샷 디렉토리 내부 비우기
  begin_op();
  ilock(dir);
  wipe_dir_content(dir); 
  dir->size = 0;
  iupdate(dir);
  iunlock(dir);
  end_op();

  // 2. /snapshot 디렉토리에서 해당 항목 삭제
  struct inode *parent = namei("/snapshot");
  if(!parent){
    iput(dir);
    return -1;
  }

  begin_op();
  ilock(parent);

  struct dirent de;
  uint off;
  struct dirent empty;
  memset(&empty, 0, sizeof(empty));

  for(off=0; off<parent->size; off+=sizeof(de)){
    if(readi(parent,(char*)&de,off,sizeof(de)) != sizeof(de)) continue;
    if(!xstrcmp(de.name,name)){
      writei(parent, (char*)&empty, off, sizeof(empty));
      break;
    }
  }
  iunlock(parent);
  end_op();

  iput(parent);
  iput(dir);
  return 0;
}

// (print_addr 등에서 사용)
int get_file_blocks_kernel(struct inode *ip, int *arr)
{
  ilock(ip);
  int cnt=0;

  for(int i=0;i<NDIRECT;i++)
    if(ip->addrs[i])
      arr[cnt++]=ip->addrs[i];

  if(ip->addrs[NDIRECT]){
    struct buf *bp=bread(ip->dev, ip->addrs[NDIRECT]);
    uint *a=(uint*)bp->data;
    for(int j=0;j<NINDIRECT;j++)
      if(a[j])
        arr[cnt++]=a[j];
    brelse(bp);
  }

  iunlock(ip);
  return cnt;
}
