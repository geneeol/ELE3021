#ifndef MLFQ_H
# define MLFQ_H

# define NMLFQ 3
# define L0 0
# define L1 1
# define L2 2

typedef struct queue {
  struct proc *items[NPROC + 1];
  int         front;
  int         rear;
} t_queue;

int	queue_get_size(t_queue *q);
int	queue_is_empty(t_queue *q);
int	queue_is_full(t_queue *q);
struct proc	*queue_get_front(t_queue *q);
struct proc	*queue_get_rear(t_queue *q);
int	enqueue(t_queue *q, struct proc *item);
int	dequeue(t_queue *q, struct proc **item);

#endif
