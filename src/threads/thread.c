#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;


/* Track nested donation depth */
int const MAX_DONATION_DEPTH = 8;

/* The load average of the system */
int load_avg;
int const INITIAL_LOAD_AVERAGE = 0;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  load_avg = 0;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
//printf("thread_create: enter. name: %s, priority: %i.\n",name,priority); //TODO
  struct thread *thread_new;
  struct kernel_thread_frame *k_thread_frame;
  struct switch_entry_frame *entry_frame;
  struct switch_threads_frame *switch_frame;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  thread_new = palloc_get_page (PAL_ZERO);
  if (thread_new == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (thread_new, name, priority);
  tid = thread_new->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  k_thread_frame = alloc_frame (thread_new, sizeof *k_thread_frame);
  k_thread_frame->eip = NULL;
  k_thread_frame->function = function;
  k_thread_frame->aux = aux;

  /* Stack frame for switch_entry(). */
  entry_frame = alloc_frame (thread_new, sizeof *entry_frame);
  entry_frame->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  switch_frame = alloc_frame (thread_new, sizeof *switch_frame);
  switch_frame->eip = switch_entry;
  switch_frame->ebp = 0;

  /* Add to run queue. */
  thread_unblock (thread_new);
//printf("thread_create. p: %u, running: %s.\n",priority,thread_current()->name); //TODO
  /* Check to see if the running thread is higher priority than current thread. */
  if( priority > thread_get_priority() )
  {
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caler had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
//printf("thread_unblock: %s. P:%u\n",t->name,t->priority); //TODO
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered(&ready_list, &t->elem, compare_priority, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
//printf("thread_current: enter.\n"); //TODO
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

//printf("thread_yield.\n");
  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, &compare_priority, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's donated_priority to NEW_DONATED_PRIORITY
   Assumes that the depth of priority has already been determined to 
   be within acceptable levels. */
void
thread_set_donated_priority (struct thread *don_thread, int new_donated_priority)
{
  if( new_donated_priority > don_thread->donated_priority )
  {
    don_thread->donated_priority = new_donated_priority;
  }
}

/* Sets the current thread's donated_priority back to previous priority */
void
thread_recall_donated_priority (struct thread *don_thread, int recall_priority)
{
  /* Check if the priority being recalled is the priority of the thread.
     Accomodates multiple donations, and recalls only the current value */
  if( recall_priority == don_thread->donated_priority )
  { 
    don_thread->donated_priority = don_thread->priority;
  }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
//printf("thread_set_priority: enter. thread: %s, old_priority: %i, new_priority: %i.\n",thread_current()->name, thread_current()->priority, new_priority); //TODO
  thread_current ()->priority = new_priority;
  thread_current ()->donated_priority = new_priority;

  priority_check_running_vs_ready();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
//printf("thread_get_priority: enter. thread: %s, priority: %i.\n",thread_current()->name, thread_current()->donated_priority);
  return thread_current ()->donated_priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current()->thread_nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->donated_priority = priority;
  t->magic = THREAD_MAGIC;
  t->depth_of_donation = 0;
  t->thread_nice = 0;  // TODO - make it inherit from parent thread

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);



/* list_less_func to compare the wakeup_ticks and priority of the two list elements,
   if first_list_elem has lower wakeup_ticks, returns true
   if first_list_elem has higher wakeup_ticks, returns false
   if first_list_elem and second_list_elem have the same wakeup_ticks, checks priority,
     if first_list_elem has higher priority returns true, else returns false
   aligns to a first come first served list, if the two elements have the same wakeup time,
     and the same priority.
   */
bool compare_wakeup_ticks (const struct list_elem *first_list_elem,
                           const struct list_elem *second_list_elem,
                           void *aux UNUSED)
{
  struct thread *first_thread = list_entry( first_list_elem, struct thread, time_elem );
  struct thread *second_thread = list_entry( second_list_elem, struct thread, time_elem );

//printf("compare_wakeup_ticks: first: %s-%"PRId64"-%i, second: %s-%"PRId64"-%i\n",first_thread->name,first_thread->wakeup_ticks,first_thread->priority,second_thread->name,second_thread->wakeup_ticks,second_thread->priority); //TODO
  if( first_thread->wakeup_ticks < second_thread->wakeup_ticks )
    return true;
  else
  {
    if( first_thread->wakeup_ticks > second_thread->wakeup_ticks )
    {
      return false;
    }
    /* if( first_thread->wakeup_ticks == second_thread->wakeup_ticks ) */
    else
    {
      if( first_thread->priority > second_thread->priority )
      {
        return true;
      }
      else
        return false;
    }
  }
}

/* list_less_func to compare the priority of the two list elements,
   if first_list_elem has higher priority, returns true
   if first_list_elem has lower or equal priority, returns false
   aligns to a first come first served list, if the two elements have the same priority.
   */
bool compare_priority (const struct list_elem *first_list_elem,
                           const struct list_elem *second_list_elem,
                           void *aux UNUSED)
{
  struct thread *first_thread = list_entry( first_list_elem, struct thread, elem );
  struct thread *second_thread = list_entry( second_list_elem, struct thread, elem );

  if( first_thread->priority > second_thread->priority )
  {
//printf("compare_priority:, greater first: %s-%i, second: %s-%i\n",first_thread->name,first_thread->priority,second_thread->name,second_thread->priority); //TODO
    return true;
  }
  else
  {

//printf("compare_priority:, not greater."); //TODO
    return false;
  }
}


/* Nested donation of priority handling */
void thread_donate_priority_chain( struct thread *donating_from, struct thread *donating_to, int donated_priority, int donated_depth )
{ 

  enum intr_level old_level;
  struct thread *high_priority_thread = donating_from;
  struct thread *low_priority_thread = donating_to;
  int donation_depth UNUSED = donated_depth;

//printf("donate_priority_chain: high: %s, priority: %u, low: %s, priority: %u.\n",high_priority_thread->name,high_priority_thread->donated_priority,low_priority_thread->name,low_priority_thread->donated_priority); //TODO

  old_level = intr_disable ();

  if( low_priority_thread->donated_priority < donated_priority )
  {
    low_priority_thread->donated_priority = donated_priority;
    high_priority_thread->donated_thread = low_priority_thread;
  }

// if the high is waiting on the low, but the low is not waiting, then low_priority_thread->waiting_lock != NULL doesn't work TODO

/*TODO  while( low_priority_thread->waiting_lock != NULL && donation_depth <= MAX_DONATION_DEPTH )
  {  
//printf("donate_priority_chain: while loop.\n");

    /* Check to see if you are trying to donate to a lower priority thread */
/*
    if( low_priority_thread->donated_priority < donated_priority)
    {
      low_priority_thread->donated_priority = donated_priority;
      high_priority_thread->donated_thread = low_priority_thread;
    }
    else
    {
      break;
    }

    high_priority_thread = low_priority_thread;
    low_priority_thread = (low_priority_thread->waiting_lock)->holder;

    donation_depth++;
  }*/

  intr_set_level (old_level);
}

/* To recall priorities through nested priorities */
void thread_recall_priority_chain( struct thread *donating_from UNUSED, struct thread *donated_to, int recall_priority UNUSED, int recall_depth UNUSED)
{
  enum intr_level old_level;
  //struct thread *high_priority_thread = donating_from;
  struct thread *low_priority_thread = donated_to;
  //struct thread *temp_thread;  /* Facilitate cleanup as traveling through donation pathway */
  //int track_depth = recall_depth;

  old_level = intr_disable ();

  low_priority_thread->donated_priority = low_priority_thread->priority;
/*
  while( low_priority_thread->donated_thread != NULL && low_priority_thread->donated_priority == recall_priority && track_depth < MAX_DONATION_DEPTH ) 
  {
    low_priority_thread->donated_priority = low_priority_thread->priority;
    low_priority_thread->depth_of_donation = 0;

    temp_thread = high_priority_thread->donated_thread;
    high_priority_thread->donated_thread = NULL;
    high_priority_thread = temp_thread;
    low_priority_thread = high_priority_thread->donated_thread;

    track_depth++;
  }
*/
  intr_set_level (old_level);
}

/* To check if the current running thread has a greater than or equal priority to the first thread on the ready queue.
   Note: must use function in thread.c, since ready_list is in thread.c.  Synch.c was not able to access this queue. */
void priority_check_running_vs_ready(void)
{

  enum intr_level old_level = intr_disable();

  if( !list_empty(&ready_list) )
  {
//printf("priority_check. %s, %s.\n", thread_current()->name, (list_entry(list_front(&ready_list),struct thread, elem)->name) ); //TODO
    if( thread_current()->donated_priority < (list_entry(list_front(&ready_list),struct thread, elem)->donated_priority ) )
    {
        thread_yield();
    }
  }

  intr_set_level (old_level);
}
