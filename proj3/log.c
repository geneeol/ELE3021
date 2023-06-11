#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
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
struct log log;

static void recover_from_log(void);
static void commit();

void
initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  struct superblock sb;
  initlock(&log.lock, "log");
  readsb(dev, &sb);
  log.start = sb.logstart;
  log.size = sb.nlog;
  log.dev = dev;
  recover_from_log();
}

// Copy committed blocks from log to their home location
static void
install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

//h 파일 연산은 begin_op와 end_op사이에서 처리한다
//  모든 파일 연산을 트랜젝션처럼 처리
// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    //h 로그에 데이터를 못쓰는 상태면 sleep
    if(log.committing)
      sleep(&log, &log.lock);
    else
    {
      log.outstanding += 1; //h 파일 시스템에 뭔가 쓴다고 선언
      release(&log.lock);
      break;
    }
  }
}

//h 싱크가 호출될 때만 디스크에 쓰게
//h 기존 커밋 매커니즘 지우고 싱크 호출 시 커밋하게 수정하래
// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing) //h 다른 사람이 로그에 쓰지 않음
    panic("log.committing");
  // 로그가 존재하면 버퍼를 비워야 함. 로그 기록은 버퍼가 가득 찼을 때만 함
  if(log.outstanding == 0 && log.lh.n > 0) {
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);
  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    //h commit을 통해 flush
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

// 데이터를 디스크에 쓰기 전에 로그를 디스크에 쓰고 해당 정보 바탕으로 
// 데이터를 디스크에 씀
void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

int sync(void)
{
  int ret;

  ret = log_write_all();
  commit();
  return (ret);
}

//h 기존: log_write를 통해 데이터 변경하면 바로바로 로그 버퍼에 기록
//  현재: log_write에서 버퍼가 가득찼을 때만 로그 버퍼에 기록
// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache with B_DIRTY.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  // 동시성등 복잡한 상황을 고려하지 않아도 되므로 로그에 쓰지 않더라도
  // 버퍼에 블록을 읽어들이고 수정했다면 버퍼를 dirty로 바로 마킹
  b->flags |= B_DIRTY;
  if (buf_is_full())
    log_write_all();
}

/******** 원본 begin_op, end_op, log_write *******/

// // called at the start of each FS system call.
// void
// begin_op(void)
// {
//   acquire(&log.lock);
//   while(1){
//     //h 로그에 데이터를 못쓰는 상태면 sleep
//     if(log.committing){
//       sleep(&log, &log.lock);
//     } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
//       // this op might exhaust log space; wait for commit.
//       sleep(&log, &log.lock);
//     } else {
//       log.outstanding += 1; //h 파일 시스템에 뭔가 쓴다고 선언
//       release(&log.lock);
//       break;
//     }
//   }
// }

// void
// end_op(void)
// {
//   int do_commit = 0;

//   acquire(&log.lock);
//   log.outstanding -= 1;
//   if(log.committing) //h 다른 사람이 로그에 쓰지 않음
//     panic("log.committing");
//   if(log.outstanding == 0){ //h 나도 안쓰고 다른 사람도 안씀 즉 flush 가능
//     do_commit = 1;
//     log.committing = 1;
//   } else {
//     // begin_op() may be waiting for log space,
//     // and decrementing log.outstanding has decreased
//     // the amount of reserved space.
//     wakeup(&log);
//   }
//   release(&log.lock);

//   if(do_commit){
//     // call commit w/o holding locks, since not allowed
//     // to sleep with locks.
//     commit(); //h 여기에서 flush 발생, 데이터를 디스크에 쓰기 전에 버퍼에 쓴 걸 로그에 쓰고 로그를 디스크에 플러쉬
//     acquire(&log.lock);
//     log.committing = 0;
//     wakeup(&log);
//     release(&log.lock);
//   }
// }

// void
// log_write(struct buf *b)
// {
//   int i;

//   if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
//     panic("too big a transaction");
//   if (log.outstanding < 1)
//     panic("log_write outside of trans");

//   acquire(&log.lock);
//   for (i = 0; i < log.lh.n; i++) {
//     if (log.lh.block[i] == b->blockno)   // log absorbtion
//       break;
//   }
//   log.lh.block[i] = b->blockno;
//   if (i == log.lh.n)
//     log.lh.n++;
//   b->flags |= B_DIRTY; // prevent eviction
//   release(&log.lock);
// }
