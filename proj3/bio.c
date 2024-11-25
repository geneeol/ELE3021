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
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};

extern struct log log;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  // TODO: stress test ... 1에서 에러 발생
  // itrunc에서 호출한 bread에서 에러 발생하는듯
  // 이 상황에 모든 버퍼가 dirty로 마킹돼서 bget실패
  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.

int 
count_dirty_buf()
{
  struct buf *b;
  int n_dirty_buf;

  acquire(&bcache.lock);

  n_dirty_buf = 0;
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev)
  {
    if ((b->flags & B_DIRTY) != 0)
      n_dirty_buf++;
  }
  
  release(&bcache.lock);
  return (n_dirty_buf);
}

int 
buf_is_full()
{
  return (count_dirty_buf() + SPARESIZE >= NBUF);
}

// int
// log_write_reserved()
// {
//   struct buf *b;
//   int n_reserved_buf;
//   int i;

//   acquire(&bcache.lock);

//   n_reserved_buf = 0;
//   for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
//   {
//     if ((b->flags & B_RESERVED) != 0)
//     {
//       b->flags &= ~B_RESERVED;
//       for (i = 0; i < log.lh.n; i++) {
//         if (log.lh.block[i] == b->blockno)   // log absorbtion
//           break;
//       }
//       log.lh.block[i] = b->blockno;
//       if (i == log.lh.n)
//         log.lh.n++;
//       b->flags |= B_DIRTY;
//       n_reserved_buf++;
//     }
//   }

//   release(&bcache.lock);
//   return (n_reserved_buf);
// }

int
log_write_all()
{
  struct buf *b;
  int n_dirty_buf;
  int i;

  acquire(&bcache.lock);

  n_dirty_buf = 0;
  for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
  {
    if ((b->flags & B_DIRTY) != 0)
    {
      for (i = 0; i < log.lh.n; i++) {
        if (log.lh.block[i] == b->blockno)   // log absorbtion
          break;
      }
      log.lh.block[i] = b->blockno;
      if (i == log.lh.n)
        log.lh.n++;
      n_dirty_buf++;
    }
  }

  release(&bcache.lock);
  return (n_dirty_buf);
}
