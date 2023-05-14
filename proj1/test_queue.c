#include <stdio.h>

#define NPROC 64


typedef struct queue {
  int	items[NPROC + 1];
  int   front;
  int   rear;
} t_queue;

int
get_size(t_queue *q)
{
  if (q->rear >= q->front)
    return (q->rear - q->front);
  else
    return (NPROC + 1 - (q->front - q->rear));
}

int
is_empty(t_queue *q)
{
  return (q->front == q->rear);
}

int
is_full(t_queue *q)
{
  return ((q->rear + 1) % (NPROC + 1) == q->front);
}

int
get_front(t_queue *q)
{
  return (q->items[(q->front + 1) % (NPROC + 1)]);
}

int
get_rear(t_queue *q)
{
  return (q->items[q->rear]);
}

int
enqueue(t_queue *q, int item)
{
  if (is_full(q))
    return (-1);
  q->rear = (q->rear + 1) % (NPROC + 1);
  q->items[q->rear] = item;
  return (0);
}

int
dequeue(t_queue *q, int *item)
{
  if (is_empty(q))
    return (-1);
  q->front = (q->front + 1) % (NPROC + 1);
  *item = q->items[q->front];
  q->items[q->front] = -1;
  return (0);
}

int
dist(int iter1, int iter2)
{
  if (iter2 >= iter1)
    return (iter2 - iter1);
  return (NPROC + 1 - (iter1 - iter2)); //h 여기 에러 가능성 높음
}

int	main(void)
{
  t_queue q;
  int i;
  int item;

  q.front = 0;
  q.rear = 0;
 
  i = 0;
  while (i < 7)
  {
	enqueue(&q, i);
	i++;
  }
  printf("size: %d\n", get_size(&q));
  printf("front: %d\n", get_front(&q));
  printf("rear: %d\n", get_rear(&q));
  printf("q.front: %d\n", q.front);
  printf("q.rear: %d\n\n", q.rear);
  i = 0;
  while (i < 7)
  {
	if (dequeue(&q, &item) == -1)
	  printf("queue is empty\n");
	printf("item: %d\n", item);
	i++;
  }
  printf("q.front: %d\n", q.front);
  printf("q.rear: %d\n", q.rear);
  printf("q.size: %d\n", get_size(&q));
  printf("\n");
  i = 0;
  while (i < 10)
    enqueue(&q, i++);

  printf("q.front: %d\n", q.front);
  printf("q.rear: %d\n", q.rear);
  printf("q.size: %d\n", get_size(&q));
  printf("get_front: %d\n", get_front(&q));
  printf("get_rear: %d\n\n", get_rear(&q));

  for (int i = 0; i < NPROC + 1; i++)
    printf("q.items[%d]: %d\n", i, q.items[i]);
  printf("\n\n");

  // printf("dist 3: %d\n", dist(2, 5));
  // printf("dist 5: %d\n", dist(8, 2));
  printf("dist 29: %d\n", dist(50, 14));

  
  return (0);

}