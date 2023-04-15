#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "mlfq.h"

int
queue_get_size(t_queue *q)
{
  if (q->rear >= q->front)
    return (q->rear - q->front);
  else
    return (NPROC + 1 - (q->front - q->rear));
}

int
queue_is_empty(t_queue *q)
{
  return (q->front == q->rear);
}

int
queue_is_full(t_queue *q)
{
  return ((q->rear + 1) % (NPROC + 1) == q->front);
}

struct proc *
queue_front(t_queue *q)
{
  return (q->items[(q->front + 1) % (NPROC + 1)]);
}

struct proc *
queue_rear(t_queue *q)
{
  return (q->items[q->rear]);
}

int
queue_push_back(t_queue *q, struct proc *item)
{
  if (queue_is_full(q))
    return (-1);
  q->rear = (q->rear + 1) % (NPROC + 1);
  q->items[q->rear] = item;
  return (0);
}

int
queue_pop(t_queue *q)
{
  if (queue_is_empty(q))
    return (-1);
  q->front = (q->front + 1) % (NPROC + 1);
  q->items[q->front] = 0;
  return (0);
}

int
queue_push_front(t_queue *q, struct proc *item)
{
  if (queue_is_full(q))
    return (-1);
  q->items[q->front] = item;
  if (q->front == 0)
    q->front = NPROC + 1;
  else
    q->front--;
  return (0);
}

int
len_from_begin(int begin, int iter)
{
  if (iter >= begin)
    return (iter - begin);
  return (NPROC + 1 - (begin - iter));
}
