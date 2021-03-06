#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

int randInRange(int n)
{
  return (int)(((double)rand()/RAND_MAX)*(n + 1));
}

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

//pthread_mutex_t shared_lock;

#define ITERATIONS 5000000
#define NUM_THREADS 128
__int128_t shared_128_int = 1;
int err_count = 0;
void log_err(__int128_t tmp, int iter)
{
  err_count++;
  unsigned int *tmp_ = (unsigned int *)&tmp;
  printf("More than 2 bits were set in 0x%x%x%x%x on iteration %d\n", tmp_[3], tmp_[2], tmp_[1], tmp_[0], iter);
}
long *bitcount;
long *bitcount_bytes[4];
struct waiter_checker_args {
  bool *all_done;
  bool *started;
};
void* waiting_checker(void* args_)
{
  bitcount = calloc(5, sizeof(long));
  long* arr = calloc(8, sizeof(long));
  bitcount_bytes[0] = arr;
  bitcount_bytes[1] = arr + 2;
  bitcount_bytes[2] = arr + 4;
  bitcount_bytes[3] = arr + 6;  
  
  struct waiter_checker_args *args = (struct waiter_checker_args *)args_;
  while(!*(args->started));
  printf("CHECKER: Started\n");

  for(int i = 0; !*(args->all_done); i++)
  {
    //pthread_mutex_lock(&shared_lock);
    //__int128_t tmp = shared_128_int;
    
    __int128_t tmp = -1;
    __atomic_compare_exchange(&shared_128_int, &tmp, &tmp, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
     //     pthread_mutex_unlock(&shared_lock);
     int* tmp_ = (int*)&tmp;
     bitcount[__builtin_popcount(tmp_[0]) + __builtin_popcount(tmp_[1]) + __builtin_popcount(tmp_[2]) + __builtin_popcount(tmp_[3])]++;

     bitcount_bytes[0][__builtin_popcount(tmp_[0])]++;
     bitcount_bytes[1][__builtin_popcount(tmp_[1])]++;
     bitcount_bytes[2][__builtin_popcount(tmp_[2])]++;
     bitcount_bytes[3][__builtin_popcount(tmp_[3])]++;
     int sleeping_for =  (int)((double)rand()/RAND_MAX * 1000 + 100);
     //printf("Sleeping for %dns\n", sleeping_for);
     //struct timespec tm = (struct timespec){ .tv_sec = 0, .tv_nsec = sleeping_for };
     //nanosleep(&tm, NULL);
  }
  printf("CHECKER: Done\n");
}

struct waiting_worker_args {
  monitor_t *m;
  int bit;
  int *count;
  bool *started;
  bool *all_done;
};
void* waiting_worker(void* arg_)
{
  struct waiting_worker_args *arg = (struct waiting_worker_args *)arg_;
  
  for(int i = 0; i < ITERATIONS; i++)
    {
      if(i == 1000)
	{
	  //printf("Thread-%d started\n", arg->bit);
	  *(arg->started) = true;

	}


      int bitNum1 = randInRange(127);
      int bitNum2 = randInRange(127);
      while(bitNum1/32 == bitNum2/32)
	{
	  bitNum1 = randInRange(127);
	  bitNum2 = randInRange(127);
	}
      __int128_t tmp = 1;
      __int128_t val = (tmp << bitNum1) + (tmp << bitNum2);
      //pthread_mutex_lock(&shared_lock);
      __atomic_compare_exchange(&shared_128_int, &shared_128_int, &val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
      //__atomic_store_16(&shared_128_int, val, __ATOMIC_SEQ_CST);
      //shared_128_int = val;
      //printf("Thread-%d: setting %d and %d\n", arg->bit, bitNum1, bitNum2);
      //pthread_mutex_unlock(&shared_lock);
      if(i == 0)
	{
	  pthread_mutex_lock(&(arg->m->lock));
	  *(arg->count) += 1;
	  if(*(arg->count) == NUM_THREADS)
	    {
	      //printf("Waking everyone up.\n");
	      pthread_cond_broadcast(&(arg->m->condition));
	    }

	  else while(*(arg->count) < NUM_THREADS)
		 {
		   //printf("Waiting, only %d are activated.\n", *(arg->count));
		   pthread_cond_wait(&(arg->m->condition), &(arg->m->lock));
		 }
	  pthread_mutex_unlock(&(arg->m->lock));

	}
    }
  *(arg->all_done) = true;
  printf("-%d-\n", arg->bit);
  //  printf("Thread-%d completed\n", arg->bit);
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

  //pthread_mutex_init(&shared_lock, NULL);
  int starting_state = (int)shared_128_int;
  int count = 0;
  monitor_t *m;
  monitor_init(noop, &m, NULL);

  bool started = false;
  bool all_done = false;
  pthread_t checker_t;
  struct waiter_checker_args checker_args = (struct waiter_checker_args){ .all_done = &all_done, .started = &started };
  pthread_create(&checker_t, NULL, waiting_checker, (void*)&checker_args);
  //printf("MAIN: Started my checker thread\n");
  struct waiting_worker_args args[NUM_THREADS];
  pthread_t tids[NUM_THREADS];
  for(int i = 0; i < NUM_THREADS; i++)
    {
      args[i] = (struct waiting_worker_args) { .count = &count, .bit = i%128, .m = m, .started = &started, .all_done = &all_done };
      pthread_create(tids + i, NULL, waiting_worker, (void*)(args + i));
    }
  pthread_join(checker_t, NULL);
  for(int i = 0; i < NUM_THREADS; i++) pthread_cancel(tids[i]);
  for(int i = 0; i < NUM_THREADS; i++) pthread_join(tids[i], NULL);
  all_done = true;
  printf("\nAll done.\n");
  double sum = (double)bitcount[0] + bitcount[1] + bitcount[2] + bitcount[3] + bitcount[4];
  printf("(Starting State: %d, Iterations: %d, Num threads: %d) - Count: 0 - %ld (%f), 1 - %ld (%f), 2 - %ld (%f), 3 - %ld (%f), 4 - %ld (%f)\n", starting_state, ITERATIONS, NUM_THREADS, bitcount[0], bitcount[0]/sum * 100, bitcount[1], bitcount[1]/sum * 100, bitcount[2], bitcount[2]/sum * 100, bitcount[3], bitcount[3]/sum *100, bitcount[4], bitcount[4]/sum *100);

  double sm1 = bitcount_bytes[0][0] + bitcount_bytes[0][1];
  double sm2 = bitcount_bytes[1][0] + bitcount_bytes[1][1];
  double sm3 = bitcount_bytes[2][0] + bitcount_bytes[2][1];
  double sm4 = bitcount_bytes[3][0] + bitcount_bytes[3][1];
  printf("(\nNum 0s, Num 1s, pc 0s, pc 1s)\n[Byte 0] (%d, %d, %f, %f)\n[Byte 1] (%d, %d, %f, %f)\n[Byte 2] (%d, %d, %f, %f)\n[Byte 3] (%d, %d, %f, %f)\n", bitcount_bytes[0][0], bitcount_bytes[0][1], bitcount_bytes[0][0]*100/sm1, bitcount_bytes[0][1]*100/sm1, bitcount_bytes[1][0], bitcount_bytes[1][1], bitcount_bytes[1][0]*100/sm2, bitcount_bytes[1][1]*100/sm2, bitcount_bytes[2][0], bitcount_bytes[2][1], bitcount_bytes[2][0]*100/sm3, bitcount_bytes[2][1]*100/sm3, bitcount_bytes[3][0], bitcount_bytes[3][1], bitcount_bytes[3][0]*100/sm4, bitcount_bytes[3][1]*100/sm4);
  pthread_exit(NULL);

}
