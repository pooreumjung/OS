// ============================================================
// FS.C — FINAL VERSION (Part 1/5)
// Fully compatible with snapshot.c final version
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

#define min(a,b) ((a)<(b)?(a):(b))

// ------------------------------------------------------------
// global block refcount (COW)
// xv6 block count <= 3000, generous 20000 allocated
// ------------------------------------------------------------
int block_refcnt[20000];
struct superblock sb;

void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// zero-out block
static void
bzero(int dev, int bno)
{
  struct buf *bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// allocate a free block
uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && (b + bi) < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){
        bp->data[bi/8] |= m;
        log_write(bp);
        brelse(bp);

        bzero(dev, b + bi);
        block_refcnt[b+bi] = 1;
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// free block respecting COW refcount
void
bfree(int dev, uint b)
{

  if(block_refcnt[b] == 0) {
    panic("bfree: double");
  }

  if(--block_refcnt[b] > 0){
    return;
  }


  struct buf *bp = bread(dev, BBLOCK(b, sb));
  int bi = b % BPB;
  int m = 1 << (bi % 8);

  if((bp->data[bi/8] & m) == 0) {
    panic("bfree: bitmap inconsistent");
  }

  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}


struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

// init inode cache + read superblock
void
iinit(int dev)
{
  initlock(&icache.lock, "icache");
  for(int i=0; i<NINODE; i++)
    initsleeplock(&icache.inode[i].lock, "inode");

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d\n",
          sb.size, sb.nblocks, sb.ninodes);
}

// get cached inode or allocate slot
struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty = 0;
  
  acquire(&icache.lock);
  for(ip = icache.inode; ip < icache.inode + NINODE; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)
      empty = ip;
  }

  if(empty == 0){
    panic("iget: no inodes");
  }
  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;

   ip->flags = 0;
  release(&icache.lock);
  return ip;
}

// allocate inode
struct inode*
ialloc(uint dev, short type)
{
  struct buf *bp;
  struct dinode *dip;

  for(int inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + (inum % IPB);

    if(dip->type == 0){
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);
      brelse(bp);

      struct inode *ip = iget(dev, inum);
      return ip;
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

void
iupdate(struct inode *ip)
{
  struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  struct dinode *dip = (struct dinode*)bp->data + (ip->inum % IPB);

  dip->type  = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size  = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));

  log_write(bp);
  brelse(bp);
}

struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

void
ilock(struct inode *ip)
{
  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode*)bp->data + (ip->inum % IPB);

    ip->type  = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size  = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));

    brelse(bp);
    ip->valid = 1;
  }
}

void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");
  releasesleep(&ip->lock);
}

void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);

  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);

    if(r == 1){
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }

  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

uint
bmap(struct inode *ip, uint bn)
{
  uint addr;
  struct buf *bp;
  uint *a;

  // DIRECT blocks
  if(bn < NDIRECT){
    addr = ip->addrs[bn];
    if(addr == 0){
      addr = balloc(ip->dev);
      ip->addrs[bn] = addr;
      iupdate(ip);
    }
    return addr;
  }
  
  // INDIRECT blocks
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    addr = ip->addrs[NDIRECT];

    if(addr == 0){
      addr = balloc(ip->dev);
      ip->addrs[NDIRECT] = addr;
      iupdate(ip);
    }

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    uint dblk = a[bn];
    if(dblk == 0){
      dblk = balloc(ip->dev);
      a[bn] = dblk;
      log_write(bp);
    }

    brelse(bp);
    return dblk;
  }

  panic("bmap: out of range");
}

// Returns "safe-to-write block number".
// If block shared → alloc new + copy.
// If indirect block shared → COW indirect block too.
// ============================================================
// ============================================================
// bcow — Return a writable block for inode ip at logical block bn
// ------------------------------------------------------------
// - 공유(refcount > 1)라면, 새 block을 만들어 복사
// - direct / indirect block 모두 지원
//
// Snapshot 구현의 핵심 함수
// ============================================================
static uint
bcow(struct inode *ip, uint bn)
{
  uint old_blk = bmap(ip, bn);

  // block이 혼자만 쓰는 block이면 그대로 사용
  if(block_refcnt[old_blk] <= 1)
    return old_blk;

  // 공유 block → copy-on-write
  uint new_blk = balloc(ip->dev);

  // copy block
  struct buf *bp_old = bread(ip->dev, old_blk);
  struct buf *bp_new = bread(ip->dev, new_blk);
  memmove(bp_new->data, bp_old->data, BSIZE);
  log_write(bp_new);
  brelse(bp_old);
  brelse(bp_new);

  block_refcnt[old_blk]--;

  // direct block
  if(bn < NDIRECT){
    ip->addrs[bn] = new_blk;
    iupdate(ip);
    return new_blk;
  }

  // indirect block COW
  uint indir = ip->addrs[NDIRECT];

  // if indirect block shared: COW indirect block too
  if(block_refcnt[indir] > 1){
    uint new_indir = balloc(ip->dev);

    struct buf *bp1 = bread(ip->dev, indir);
    struct buf *bp2 = bread(ip->dev, new_indir);
    memmove(bp2->data, bp1->data, BSIZE);

    uint *a = (uint*)bp2->data;
    a[bn - NDIRECT] = new_blk;

    log_write(bp2);
    brelse(bp1);
    brelse(bp2);

    block_refcnt[indir]--;
    ip->addrs[NDIRECT] = new_indir;
    block_refcnt[new_indir] = 1;

    iupdate(ip);
    return new_blk;
  }

  // normal indirect write
  struct buf *bp = bread(ip->dev, indir);
  uint *a = (uint*)bp->data;
  a[bn - NDIRECT] = new_blk;
  log_write(bp);
  brelse(bp);

  iupdate(ip);
  return new_blk;
}

void
itrunc(struct inode *ip)
{
  struct buf *bp;
  uint *a;

  // ----------------------------------------
  // ★ 스냅샷 inode는 절대 block을 free하면 안 된다 ★
  //   (shared block 구조 때문에 free 시 double free 발생)
  // ----------------------------------------
  if (ip->flags & I_SNAP) {
    // snapshot inode는 단순히 크기만 0으로
    ip->size = 0;
    iupdate(ip);
    return;
  }

  // 기존 코드 --------------------------------

  // free direct blocks
  for(int i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // free indirect blocks
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;

    for(int j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }

    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}



void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot = 0; tot < n; tot += m, off += m, dst += m){
    uint blk = bmap(ip, off / BSIZE);
    bp = bread(ip->dev, blk);

    m = min(n - tot, BSIZE - (off % BSIZE));
    memmove(dst, bp->data + (off % BSIZE), m);

    brelse(bp);
  }

  return n;
}

int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  // ------------------------------------------------------------
  // snapshot 파일은 절대 수정 불가
  // ------------------------------------------------------------
  if(ip->flags & I_SNAP)
    return -1;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  uint end_off = off + n;

  // ------------------------------------------------------------
  // COW-safe write loop
  // ------------------------------------------------------------
  for(tot = 0; tot < n; tot += m, off += m, src += m){
    uint bn = off / BSIZE;

    // COW-safe physical block number
    uint writable_blk = bcow(ip, bn);

    bp = bread(ip->dev, writable_blk);

    m = min(n - tot, BSIZE - (off % BSIZE));
    memmove(bp->data + (off % BSIZE), src, m);

    log_write(bp);
    brelse(bp);
  }

  // ------------------------------------------------------------
  // inode 크기 업데이트
  // ------------------------------------------------------------
  if(n > 0 && end_off > ip->size){
    ip->size = end_off;
    iupdate(ip);
  }

  return n;
}

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(uint off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");

    if(de.inum == 0)
      continue;

    if(namecmp(name, de.name) == 0){
      if(poff)
        *poff = off;
      return iget(dp->dev, de.inum);
    }
  }

  return 0;
}

int
dirlink(struct inode *dp, char *name, uint inum)
{
  struct dirent de;

  // 이미 동일 이름 존재 → 실패
  if(dirlookup(dp, name, 0) != 0)
    return -1;

  // 빈 엔트리 찾기
  for(uint off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");

    if(de.inum == 0){
      // 여기다 쓴다
      memset(&de, 0, sizeof(de));
      strncpy(de.name, name, DIRSIZ);
      de.inum = inum;

      if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
        panic("dirlink write slot");

      return 0;
    }
  }

  // 빈 자리 없으면 append
  memset(&de, 0, sizeof(de));
  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;

  if(writei(dp, (char*)&de, dp->size, sizeof(de)) != sizeof(de))
    panic("dirlink append write");

  return 0;
}

// "////a///b//c" → "a", "b", "c"
// name 에 DIRSIZ 이하로 복사
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  // leading '/' 모두 스킵
  while(*path == '/')
    path++;

  if(*path == 0)
    return 0;

  s = path;
  while(*path != '/' && *path != 0)
    path++;

  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }

  while(*path == '/')
    path++;

  return path;
}

static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  // 절대경로 → root inode 획득
  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){

    ilock(ip);

    if(ip->type != T_DIR){
      // 디렉토리가 아닌데 하위 탐색 → 실패
      iunlockput(ip);
      return 0;
    }

    if(nameiparent && *path == 0){
      // 마지막 parent 요구하는 경우
      iunlock(ip);
      return ip;
    }

    next = dirlookup(ip, name, 0);
    if(next == 0){
      iunlockput(ip);
      return 0;
    }

    iunlockput(ip);
    ip = next;
  }

  if(nameiparent){
    iput(ip);
    return 0;
  }

  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}



