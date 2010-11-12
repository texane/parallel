#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>


typedef struct preemptpoint
{
  volatile uintptr_t addr_towrite;
} preemptpoint_t;


static pthread_barrier_t global_barrier;
static preemptpoint_t global_pp; 


static void enter_preemptpoint(void)
{
  printf("enter_preemptpoint\n");
  fflush(stdout);
  pthread_exit(NULL);
}


static void signal_preemptpoint
(preemptpoint_t* pp)
{
  /* insn buffer */
  unsigned char insn[8];

  /* relative addr */
#define CALL_INSN_SIZE 5
  const int32_t reladdr = (int32_t)
    (long)(uintptr_t)enter_preemptpoint -
    ((long)pp->addr_towrite + (long)CALL_INSN_SIZE);

  /* <call> <addr_tocall> */
  *(insn + 0) = 0xe9;
  *(uint32_t*)(insn + 1) = reladdr;
  memset(insn + CALL_INSN_SIZE, 0x90, sizeof(insn) - CALL_INSN_SIZE);

  /* write the instruction buffer */
  *(volatile uint64_t*)pp->addr_towrite = *(uint64_t*)insn;
  __sync_synchronize();
}


extern uintptr_t __addr_towrite;

#define make_preemptpoint(__pp)				\
do {							\
  const uintptr_t page_addr =				\
    ((uintptr_t)&__addr_towrite) & ~(0x1000UL - 1UL);	\
  mprotect((void*)page_addr, 0x1000,			\
    PROT_READ | PROT_WRITE | PROT_EXEC);		\
  (__pp)->addr_towrite = (uintptr_t)&__addr_towrite;	\
} while (0)


#define test_preemptpoint(__pp)		\
do {					\
  __asm__ __volatile__			\
  (					\
   ".align 8, 0x90 \n\t"		\
   ".globl __addr_towrite \n\t"		\
   "__addr_towrite: \n\t"		\
   ".byte 0xe9 \n\t"			\
   ".long 0x00000000 \n\t"		\
   ".align 8, 0x90 \n\t"		\
  );					\
} while (0)


static void* slave_entry(void* foo)
{
  make_preemptpoint(&global_pp);
  pthread_barrier_wait(&global_barrier);

  while (1)
  {
    /* do some processing */

    test_preemptpoint(&global_pp);
  }

  return NULL;
}


int main
(int ac, char** av)
{
  pthread_t thread;

  pthread_barrier_init(&global_barrier, NULL, 2);
  pthread_create(&thread, NULL, slave_entry, NULL);
  pthread_barrier_wait(&global_barrier);
  pthread_barrier_destroy(&global_barrier);

  /* wait a bit and signal */
  usleep(1000000);
  printf("signaling\n"); fflush(stdout);
  signal_preemptpoint(&global_pp);

  pthread_join(thread, NULL);

  return 0;
}
