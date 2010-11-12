#ifndef TICK_H_INCLUDED
# define TICK_H_INCLUDED



#include <stdint.h>


/* uint64_t union */

union uint64_union
{
  uint64_t value;
  struct
  {
    uint32_t lo;
    uint32_t hi;
  } sub;
};

typedef union uint64_union tick_counter_t;


static inline void tick_read(tick_counter_t* c)
{
  __asm__ __volatile__("rdtsc" : "=a" (c->sub.lo), "=d" (c->sub.hi));
}


#endif /* ! TICK_H_INCLUDED */
