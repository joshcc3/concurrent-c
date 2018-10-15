#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <math.h>

// The monitor closure
typedef struct monitor {
  pthread_mutex_t lock;
  pthread_cond_t condition;
  bool (*condition_test)(void*);
  void*extra;
} monitor_t;

void monitor_notify(monitor_t *m) { pthread_cond_signal(&m->condition); }

void monitor_broadcast(monitor_t *m) { pthread_cond_broadcast(&m->condition); }

void monitor_wait(monitor_t *m)
{
  assert(m);
  pthread_mutex_lock(&m->lock);
  while(!m->condition_test(m->extra)) pthread_cond_signal(&m->condition);
  assert(m->condition_test(m->extra));
  pthread_mutex_unlock(&(m->lock));
}

void monitor_deinit(monitor_t *m)
{
  assert(m);
  pthread_mutex_destroy(&(m->lock));
  //pthread_cond_destory(&(m->condition));
  free(m);
}

void monitor_init(bool (*c)(void*), monitor_t **m, void*extra) 
{
  assert(m && c);
  *m = (monitor_t*)malloc(sizeof(monitor_t));
  
  pthread_mutex_init(&((*m)->lock), NULL);
  pthread_cond_init(&((*m)->condition), NULL);
  (*m)->condition_test = c;
  (*m)->extra = extra;

  assert(*m != NULL);
}


#define ITERATIONS 1000000
#define NUM_THREADS 1024
__int128_t shared = 3;
int err_count = 0;
bool started = false;
void log_err(__int128_t tmp, int iter)
{
  err_count++;
  unsigned int *tmp_ = (unsigned int *)&tmp;
  printf("More than 2 bits were set in 0x%x%x%x%x on iteration %d\n", tmp_[3], tmp_[2], tmp_[1], tmp_[0], iter);
}
long bitcount[4];

struct waiter_checker_args {
  bool *all_done;
  monitor_t *m;
  int* count;
};
void* waiting_checker(void* args_)
{
  struct waiter_checker_args* args = (struct waiter_checker_args*) args_;
  pthread_mutex_lock(&(args->m->lock));
  while(*(args->count) < NUM_THREADS)
    {
      printf("About to sleep\n");
      pthread_cond_wait(&(args->m->condition), &(args->m->lock));
    }
  pthread_mutex_unlock(&(args->m->lock));
  
  printf("CHECKER: Started\n");
  __int128_t tmp = shared;
  for(int i = 0; !*(args->all_done); i++)
    {
      tmp = shared;
      bitcount[__builtin_popcount(tmp)]++;
    }
}

void* checker(void* all_done)
{
  printf("CHECKER: Started\n");
  __int128_t tmp = shared;
  for(int i = 0; !*(bool*)all_done; i++)
    {
      tmp = shared;
      bitcount[__builtin_popcount(tmp)]++;

    }
}


void* worker(void* arg)
{
  //  struct timespec sleep_time;
  int *toggleBits = (int*)arg;
  //printf("[%d, %d] Thread created\n", toggleBits[0], toggleBits[1]);
  __int128_t val = 1;
  val = val << *toggleBits;
  for(int i = 0; i < ITERATIONS; i++)
    {
      shared = val;
    }

  //printf("Thread-%d completed\n", toggleBits[0]);

}

struct waiting_worker_args {
  monitor_t *m;
  int bit;
  int *count;
};
void* waiting_worker(void* arg_)
{
  struct waiting_worker_args *arg = (struct waiting_worker_args *)arg_;
  
  __int128_t val = 1;
  val = val << arg->bit;

  pthread_mutex_lock(&(arg->m->lock));
  *(arg->count) += 1;
  if(*(arg->count) == NUM_THREADS)
    {
      printf("Waking everyone up.\n");
      pthread_cond_broadcast(&(arg->m->condition));
    }

  else while(*(arg->count) < NUM_THREADS)
	 {
	   printf("Waiting, only %d are activated.\n", *(arg->count));
	   pthread_cond_wait(&(arg->m->condition), &(arg->m->lock));
	 }
  pthread_mutex_unlock(&(arg->m->lock));

  for(int i = 0; i < ITERATIONS; i++)
    {
      shared = val;
    }

  printf("Thread-%d completed\n", arg->bit);
}

bool noop(void* null) { return true; }

int main()
{
  // Test whether reads and writes to a 128 bit value are atomic or not
  /*
    Spawn 500 threads each of which writes all 0s except to 2 bits which it toggles.
    A check thread continusouly reads from the value and reports if it ever sees
    a state where more than 1 bit is 1.
   */
  int count = 0;
  monitor_t *m;
  monitor_init(noop, &m, NULL);

  bool all_done = false;
  pthread_t checker_t;
  struct waiter_checker_args checker_args = (struct waiter_checker_args){ .all_done = &all_done, .m = m, .count = &count };
  pthread_create(&checker_t, NULL, waiting_checker, (void*)&checker_args);
  printf("MAIN: Started my checker thread\n");
  struct waiting_worker_args args[NUM_THREADS];
  pthread_t tids[NUM_THREADS];
  for(int i = 0; i < NUM_THREADS; i++)
    {
      args[i] = (struct waiting_worker_args) { .count = &count, .bit = i%128, .m = m};
      pthread_create(tids + i, NULL, waiting_worker, (void*)(args + i));
      started = true;
    }
  for(int i = 0; i < NUM_THREADS; i++) pthread_join(tids[i], NULL);
  double sum = (double)bitcount[0] + bitcount[1] + bitcount[2];
  printf("Count: 0 - %ld (%f), 1 - %ld (%f), 2 - %ld (%f)\n", bitcount[0], bitcount[0]/sum * 100, bitcount[1], bitcount[1]/sum * 100, bitcount[2], bitcount[2]/sum * 100);
  all_done = true;
  pthread_exit(NULL);

}