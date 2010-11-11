#ifndef CONC_RANGE_H_INCLUDED
# define CONC_RANGE_H_INCLUDED


/* implement a concurrent range whose semantic is
   similar to a THE queue. this implementation assumes
   there is only one sequential and one parallel
   extractor (resp. pop_front and pop_back). this is
   ensured by the concurrent version of the xkaapi
   runtime (resp. sequential work and splitter).

   locking must be used to solve concurrency issuses
   between pop_back and set since they both write the
   _end pointer. full locking is used for debugging
   and locks everywhere the range is accessed.
 */


#include <limits.h>


#define CONFIG_USE_FULL_LOCK 0

typedef long conc_size_t;
#define CONC_SIZE_MIN LONG_MIN
#define CONC_SIZE_MAX LONG_MAX


typedef struct conc_range
{
  volatile long _lock;

  /* separated cache lines since concurrent update */
  volatile conc_size_t _beg __attribute__((aligned(64)));
  volatile conc_size_t _end __attribute__((aligned(64)));

} conc_range_t;


#define CONC_RANGE_INITIALIZER(__beg, __end) { 0L, __beg, __end }

static inline void __full_barrier(void)
{
  __asm__ __volatile__ ("mfence\n\t":::"memory");
}

static inline void __slowdown(void)
{
  __asm__ __volatile__ ("pause\n\t");
}


static inline void conc_range_init
(conc_range_t* cr, conc_size_t beg, conc_size_t end)
{
  cr->_lock = 0L;
  cr->_beg = beg;
  cr->_end = end;
}


static inline void conc_range_empty
(conc_range_t* cr)
{
  cr->_beg = CONC_SIZE_MAX;
}


static inline void __lock_range(conc_range_t* cr)
{
  while (1)
  {
    if (__sync_bool_compare_and_swap(&cr->_lock, 0, 1))
      break ;
    __slowdown();
  }
}

static inline void __unlock_range(conc_range_t* cr)
{
  __full_barrier();
  cr->_lock = 0L;
}


#if CONFIG_USE_FULL_LOCK /* full locking */

static inline void conc_range_pop_front
(conc_range_t* cr, conc_size_t* beg, conc_size_t* end, conc_size_t max_size)
{
  conc_size_t size;

  __lock_range(cr);

  size = cr->_end - cr->_beg;
  if (max_size > size)
    max_size = size;

  *beg = cr->_beg;
  *end = *beg + max_size;

  cr->_beg = *end;

  __unlock_range(cr);
}

static inline int conc_range_pop_back
(conc_range_t* cr, conc_size_t* beg, conc_size_t* end, conc_size_t size)
{
  int res = -1;

  /* disable gcc warning */
  *beg = 0;
  *end = 0;

  __lock_range(cr);

  if (size > (cr->_end - cr->_beg))
    goto unlock_return;

  res = 0;

  *end = cr->_end;
  *beg = *end - size;
  cr->_end = *beg;

 unlock_return:
  __unlock_range(cr);

  return res;
}

static inline conc_size_t conc_range_size
(conc_range_t* cr)
{
  conc_size_t size;

  __lock_range(cr);
  size = cr->_end - cr->_beg;
  __unlock_range(cr);

  return size;
}

static inline void conc_range_set
(conc_range_t* cr, conc_size_t beg, conc_size_t end)
{
  __lock_range(cr);
  cr->_beg = beg;
  cr->_end = end;
  __unlock_range(cr);
}

#else /* CONFIG_USE_FULL_LOCK == 0 */

static inline void conc_range_pop_front
(conc_range_t* cr, conc_size_t* beg, conc_size_t* end, conc_size_t max_size)
{
  /* concurrency requirements: pop_back */

  /* it is possible the returned range is empty,
     but this function never fails, contrary to
     pop_back (ie. sequential extraction always
     succeeds)
   */

  conc_size_t size = max_size;

  cr->_beg += size;
  __full_barrier();

  if (cr->_beg <= cr->_end)
    goto no_conflict;

  /* handle conflict */

  cr->_beg -= size;

  __lock_range(cr);

  size = cr->_end - cr->_beg;
  if (size > max_size)
    size = max_size;

  cr->_beg += size;

  __unlock_range(cr);

 no_conflict:
  *end = cr->_beg;
  *beg = *end - size;
}

static inline int conc_range_pop_back
(conc_range_t* cr, conc_size_t* beg, conc_size_t* end, conc_size_t size)
{
  /* return value a boolean. in case of a conflict with
     pop_front, this side fails and false is returned
   */

  /* concurrency requirements:
     conc_range_set
     conc_range_pop_front
   */

  int res = -1;

  /* disable gcc warning */
  *beg = 0;
  *end = 0;

  __lock_range(cr);

  cr->_end -= size;
  __full_barrier();

  if (cr->_end < cr->_beg)
  {
    cr->_end += size;
    goto unlock_return;
  }

  res = 0;

  *beg = cr->_end;
  *end = *beg + size;

 unlock_return:
  __unlock_range(cr);

  return res;
}

static inline conc_size_t conc_range_size
(conc_range_t* cr)
{
  return cr->_end - cr->_beg;
}

static inline void conc_range_set
(conc_range_t* cr, conc_size_t beg, conc_size_t end)
{
  /* concurrency requirements:
     conc_range_pop_back
   */

  /* NOT calling this routine concurrently
     with pop_front ensures _beg wont move.
     this is required to avoid underflows
   */

  __lock_range(cr);

  /* writes not reordered on ia32 */
  cr->_beg = CONC_SIZE_MAX;
  cr->_end = end;
  cr->_beg = beg;

  __unlock_range(cr);
}

#endif /* CONFIG_USE_FULL_LOCK */


#if 0 /* use_case */

#include <stdio.h>
#include "conc_range.h"

static conc_range_t range;

static void worker_thread(void* args)
{
  conc_size_t i, j;

  while (not_signaled)
  {
#define WORKER_SIZE 10
    if (conc_range_pop_back(&range, &i, &j, WORKER_SIZE) == -1)
      continue ;

    /* pop_back succeed, process [i, j[ range */
  }
}

static void master_thread(void* args)
{
  conc_size_t i, j;

 redo_at_infinitum:
  /* the range can be set concurrently with pop_back */
  conc_range_set(&range, 0, 1000);

#define MASTER_SIZE 20
  while (1)
  {
    conc_range_pop_front(&range, &i, &j, MASTER_SIZE);
    if (i == j) break;

    /* process [i, j[ range */    
  }

  goto redo_at_infinitum;
}

#endif /* use_case */


#endif /* ! CONC_RANGE_H_INCLUDED */
