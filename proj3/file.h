struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};


//h inode자체를 공유하면 문제 발생 가능
//h 심볼릭 링크 A라는 파일 B라는 파일로 리다이렉션 되게끔
//h 파일 타입을 수정해서 심볼릭 링크가 가리키는 파일로 리다이렉션되게s
// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  // 블락포인터를 uint로 관리중.
  // addrs에 +3은 3개의 인덱스 블락을 의미함
  uint addrs[NDIRECT+3];
};

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
