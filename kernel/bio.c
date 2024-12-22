// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define HASH(id) id % NBUCKET

struct hashbuf{
  struct buf head;
  struct spinlock lk;
};

struct {
  struct hashbuf buckets[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
} bcache;

void
binit(void)
{
  struct buf *b;
  for(int i = 0; i < NBUCKET; i++){
    char lockname[16];  // 假设最大支持 15 位数字
    memset(lockname, 0, sizeof(lockname));
    snprintf(lockname, sizeof(lockname), "bcache_%d", i);
    initlock(&bcache.buckets[i].lk, lockname);
    // 初始化每个bucket head指向自身(NBUCKET个双链表)
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // 全部挂载到buckets[0]上面,bget miss的时候进行rehash
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int bid = HASH(blockno);
  // 先获取当前bucket的锁
  acquire(&bcache.buckets[bid].lk);
  // 当前散列桶的头节点
  struct buf *head = &bcache.buckets[bid].head;
  // Is the block already cached?
  for(b = head->next; b != head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      // 记录使用的时间戳
      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);

      release(&bcache.buckets[bid].lk);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  b = 0;
  struct buf* tmp;
  // Q1 转变思路,先找到一个空闲且timestamp最小的buf
  for(int bi = bid, cycle = 0; cycle != NBUCKET; bi = (bi+1) % NBUCKET){
    cycle++;
    // Q2
    if(bi != bid){
      // t1 获取 3 5 bucket
      // t2 获取 5 3 bucket
      // 防止buckets[bid]重复获取同一把锁, 同时排除死锁情况
      if(!holding(&bcache.buckets[bi].lk)){
        acquire(&bcache.buckets[bi].lk);
        // printf("%d acquire: %s\n", bi, bcache.buckets[bi].lk.name);
      }else{  // 已经有锁了直接跳到下一个桶!!
        continue;
      }
    }
    // 测试数据太水！！
    // if(!holding(&bcache.buckets[bi].lk))
    //   acquire(&bcache.buckets[bi].lk);
    struct buf* curhead = &bcache.buckets[bi].head;
    // 遍历当前桶中的链表节点
    for(tmp = curhead->next; tmp != curhead; tmp = tmp->next){
      if(tmp->refcnt == 0 && (!b || tmp->timestamp < b->timestamp)){
        b = tmp;
      }
    }
    if(b){
      // 在其他bucket找到buf, 移到当前bucket中
      if(bi != bid){
        b->next->prev = b->prev;
        b->prev->next = b->next;
        // 头插法
        b->next = head->next;
        b->prev = head;
        head->next->prev = b;
        head->next = b;
        release(&bcache.buckets[bi].lk);
      }
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      // 更新时间戳
      acquire(&tickslock);
      b->timestamp = ticks;
      release(&tickslock);

      release(&bcache.buckets[bid].lk);
      acquiresleep(&b->lock);
      return b;
    }
    // Q3 不能释放当前编号为bid桶的锁
    if(bi != bid)
      release(&bcache.buckets[bi].lk);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the tail of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int bucketid = HASH(b->blockno);
  acquire(&bcache.buckets[bucketid].lk);
  b->refcnt--;

  acquire(&tickslock);
  b->timestamp = ticks;
  release(&tickslock);

  release(&bcache.buckets[bucketid].lk);
}

void
bpin(struct buf *b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lk);
  b->refcnt++;
  release(&bcache.buckets[bid].lk);
}

void
bunpin(struct buf *b) {
  int bid = HASH(b->blockno);
  acquire(&bcache.buckets[bid].lk);
  b->refcnt--;
  release(&bcache.buckets[bid].lk);
}


