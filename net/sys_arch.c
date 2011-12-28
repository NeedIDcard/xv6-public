#include "lwip/sys.h"
#include "arch/sys_arch.h"

#include "queue.h"
#include "kernel.h"
#include "proc.h"
#include "cpu.h"

#define DIE panic(__func__)

static struct {
  struct spinlock lk;
  struct cpu *cpu;
  u64 depth;
} lwprot;

//
// mbox
//
err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
  if (size > MBOXSLOTS) {
    cprintf("sys_mbox_new: size %u\n", size);
    return ERR_MEM;
  }
  mbox->head = 0;
  mbox->tail = 0;
  mbox->invalid = 0;
  initlock(&mbox->s, "lwIP mbox");
  initcondvar(&mbox->c, "lwIP mbox");

  return ERR_OK;
}

void
sys_mbox_set_invalid(sys_mbox_t *mbox)
{
  mbox->invalid = 1;
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
  DIE;
}

int
sys_mbox_valid(sys_mbox_t *mbox)
{
  return !mbox->invalid;
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
  acquire(&mbox->s);
  while (mbox->head - mbox->tail == MBOXSLOTS)
    cv_sleep(&mbox->c, &mbox->s);
  mbox->msg[mbox->head % MBOXSLOTS] = msg;
  mbox->head++;
  release(&mbox->s);
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
  if (mbox->head != mbox->tail)
    panic("sys_mbox_free");
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
  u64 start, to;
  u32 r;

  acquire(&mbox->s);
  start = nsectime();
  to = (u64)timeout*1000000 + start;
  while (mbox->head-mbox->tail == 0) {
    if (timeout != 0) {
      if (to < nsectime()) {
        r = SYS_ARCH_TIMEOUT;
        goto done;
      }
      cv_sleepto(&mbox->c, &mbox->s, to);
    } else {
      cv_sleep(&mbox->c, &mbox->s);      
    }
  }
  r = nsectime()-start;
  *msg = mbox->msg[mbox->tail % MBOXSLOTS];
  mbox->tail++;

done:
  release(&mbox->s);
  return r;
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
  DIE;
}

//
// sem
//
err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
  initlock(&sem->s, "lwIP sem");
  initcondvar(&sem->c, "lwIP condvar");
  sem->count = count;
  return ERR_OK;
}

void
sys_sem_free(sys_sem_t *sem)
{
  DIE;
}

void
sys_sem_set_invalid(sys_sem_t *sem)
{
  DIE;
}

int
sys_sem_valid(sys_sem_t *sem)
{
  DIE;
}

void
sys_sem_signal(sys_sem_t *sem)
{
  acquire(&sem->s);  
  sem->count++;
  cv_wakeup(&sem->c);
  release(&sem->s);
}

u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
  u64 start, to;
  u32 r;

  acquire(&sem->s);
  start = nsectime();
  to = (u64)timeout*1000000 + start;
  while (sem->count == 0) {
    if (timeout != 0) {
      if (to < nsectime()) {
        r = SYS_ARCH_TIMEOUT;
        goto done;
      }
      cv_sleepto(&sem->c, &sem->s, to);
    } else {
      cv_sleep(&sem->c, &sem->s);      
    }
  }
  r = nsectime()-start;
  sem->count--;

done:
  release(&sem->s);
  return r;
}

//
// protect
//
sys_prot_t
sys_arch_protect(void)
{
  sys_prot_t r;

  pushcli();
  if (lwprot.cpu == mycpu())
    r = lwprot.depth++;
  else {
    acquire(&lwprot.lk);
    r = lwprot.depth++;
    lwprot.cpu = mycpu();
  }
  popcli();

  return r;
}

void
sys_arch_unprotect(sys_prot_t pval)
{
  if (lwprot.cpu != mycpu() || lwprot.depth == 0)
    panic("sys_arch_unprotect");
  lwprot.depth--;
  if (lwprot.depth == 0) {
    lwprot.cpu = NULL;
    release(&lwprot.lk);
  }
}

//
// thread
//
sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
               int stacksize, int prio)
{
  struct proc *p;
  
  p = threadalloc(thread, arg);
  if (p == NULL)
    panic("lwip: sys_thread_new");
  safestrcpy(p->name, name, sizeof(p->name));

  acquire(&p->lock);
  p->state = RUNNABLE;
  addrun(p);
  release(&p->lock);

  return p;
}

//
// init
//
void
sys_init(void)
{
  initlock(&lwprot.lk, "lwIP lwprot");
}
