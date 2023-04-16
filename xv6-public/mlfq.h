#ifndef MLFQ_H // TODO: 헤더가드 유효한지 확인
# define MLFQ_H

# define NMLFQ 3

enum QLEVEL {
  L0 = 0,
  L1 = 1,
  L2 = 2
};
typedef struct queue {
  struct proc *items[NPROC + 1];
  int         front;
  int         rear;
} t_queue;

int	queue_get_size(t_queue *q);
int	queue_is_empty(t_queue *q);
int	queue_is_full(t_queue *q);
struct proc	*queue_front(t_queue *q);
struct proc	*queue_rear(t_queue *q);
int	queue_push_back(t_queue *q, struct proc *item);
int queue_pop(t_queue *q);
int queue_push_front(t_queue *q, struct proc *item);
int dist_between_iters(int begin, int iter);

#endif
