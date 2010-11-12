#include <stdlib.h>
#include <stdio.h> 
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <sched.h>
#include <pthread.h>


#define ARRAY_SIZE ((1024 * 1024) / sizeof(double))
static double array[ARRAY_SIZE] __attribute__((aligned(64)));

static pthread_barrier_t barrier;
static volatile int is_done = 0;
static volatile int is_read = 0;


static inline int enter_or_done(void)
{
  pthread_barrier_wait(&barrier);
  if (is_done)
    return -1;
  return 0;
}

#include <xmmintrin.h>

static void prefetch(unsigned char* addr, size_t size)
{
#define STRIDE_SIZE 64
  for (size_t i = 0; i < size; i += STRIDE_SIZE)
  {
#if 0
    _mm_prefetch((void*)(addr + i), _MM_HINT_T2);
#else
    volatile unsigned char a = addr[i + 0];
    volatile unsigned char b = addr[i + 1];
    __asm__ __volatile__ (""::"m"(a), "m"(b));
#endif
  }
  usleep(1000);
}

static void* reader_entry(void* p)
{
#if 1
  prefetch((unsigned char*)array, sizeof(array));
#endif

  pthread_barrier_wait(&barrier);

  while (enter_or_done() != -1)
  {
#if 0
    for (size_t i = ARRAY_SIZE; i > 0; --i)
    {
      volatile double value = array[i - 1];
#else
    for (size_t i = 0; i < ARRAY_SIZE; ++i)
    {
      volatile double value = array[i];
#endif
      __asm__ __volatile__ (""::"m"(value));
    }

    __sync_synchronize();
    is_read = 1;
  }

  return NULL;
}

static void* writer_entry(void* p)
{
  srand(getpid() * time(0));

  pthread_barrier_wait(&barrier);

  while (enter_or_done() != -1)
  {
    while (is_read == 0)
    {
#if 1
      ++array[rand() % ARRAY_SIZE];
      __sync_synchronize();
#endif
    }
  }

  return NULL;
}

int main(int ac, char** av)
{
  struct timeval tms[3];
  pthread_t threads[2];

  pthread_barrier_init(&barrier, NULL, 3);

  pthread_attr_t attr;
  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);
  CPU_SET(1, &cpuset);
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

  pthread_attr_init(&attr);

  CPU_ZERO(&cpuset);
  CPU_SET(14, &cpuset);
  pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
  pthread_create(&threads[0], &attr, writer_entry, NULL);

  CPU_ZERO(&cpuset);
  CPU_SET(15, &cpuset);
  pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
  pthread_create(&threads[1], &attr, reader_entry, NULL);

  pthread_barrier_wait(&barrier);

  for (size_t i = 0; i < 1; ++i)
  {
    gettimeofday(&tms[0], NULL);
    is_read = 0;
    pthread_barrier_wait(&barrier);
    while (is_read == 0)
      __asm__ __volatile__ ("nop\n\t");
    gettimeofday(&tms[1], NULL);

    timersub(&tms[1], &tms[0], &tms[2]);
    printf("%lf\n", tms[2].tv_sec * 1E6 + tms[2].tv_usec);
  }

  is_done = 1;
  pthread_barrier_wait(&barrier);

  pthread_join(threads[0], NULL);
  pthread_join(threads[1], NULL);

  return 0;
}
