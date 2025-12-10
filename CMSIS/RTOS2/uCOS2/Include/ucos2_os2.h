#ifndef UCOS2_OS2_H_
#define UCOS2_OS2_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "cmsis_os2.h"
#include "ucos_ii.h"

/*
 * Configuration validation: the CMSIS-RTOS2 wrapper expects a number of uC/OS-II
 * features to be enabled. Fail early if the project configuration is incompatible.
 */
#if (OS_TASK_CREATE_EN < 1u) || (OS_TASK_CREATE_EXT_EN < 1u)
#error "uCOS-II CMSIS-RTOS2 wrapper requires OSTaskCreate/OSTaskCreateExt."
#endif

#if (OS_TASK_NAME_EN < 1u)
#error "Enable task names (OS_TASK_NAME_EN) to support CMSIS thread naming."
#endif

#if (OS_TASK_DEL_EN < 1u)
#error "Enable task deletion (OS_TASK_DEL_EN) for CMSIS thread lifecycle control."
#endif

#if (OS_FLAG_EN < 1u) || (OS_MAX_FLAGS == 0u)
#error "Enable uCOS-II event flags (OS_FLAG_EN) for CMSIS thread/event flags support."
#endif

#if (OS_FLAG_ACCEPT_EN < 1u)
#error "Enable OSFlagAccept (OS_FLAG_ACCEPT_EN) for CMSIS thread flags."
#endif

#if (OS_FLAG_QUERY_EN < 1u)
#error "Enable OSFlagQuery (OS_FLAG_QUERY_EN) for CMSIS thread flags."
#endif

#if (OS_SEM_EN < 1u)
#error "Enable uCOS-II semaphores (OS_SEM_EN) for CMSIS semaphore API."
#endif

#if (OS_MUTEX_EN < 1u)
#error "Enable uCOS-II mutexes (OS_MUTEX_EN) for CMSIS mutex API."
#endif

#if (OS_TASK_CHANGE_PRIO_EN < 1u)
#error "Enable OSTaskChangePrio() support (OS_TASK_CHANGE_PRIO_EN) for CMSIS priority control."
#endif

#if (OS_Q_EN < 1u) || (OS_MAX_QS == 0u)
#error "Enable uCOS-II queues (OS_Q_EN) for CMSIS message queue API."
#endif

#if (OS_MEM_EN < 1u) || (OS_MAX_MEM_PART == 0u)
#error "Enable uCOS-II memory partitions (OS_MEM_EN) for CMSIS memory pool API."
#endif

#if (OS_TMR_EN < 1u)
#error "Enable uCOS-II timers (OS_TMR_EN) for CMSIS timer API."
#endif

#if (OS_SCHED_LOCK_EN < 1u)
#error "Enable scheduler lock support (OS_SCHED_LOCK_EN) for osKernelLock/osKernelUnlock."
#endif

#define UCOS2_PRIORITY_LEVELS          50u
#define UCOS2_PRIORITY_GUARD           3u    /* Reserve Idle, Stat, Timer slots */

#if (OS_LOWEST_PRIO <= (UCOS2_PRIORITY_LEVELS + UCOS2_PRIORITY_GUARD))
#error "Increase OS_LOWEST_PRIO to accommodate CMSIS-RTOS2 priorities."
#endif

#if !defined(OS_APP_HOOKS_EN) || (OS_APP_HOOKS_EN < 1u)
#warning "OS_APP_HOOKS_EN is disabled: CMSIS wrapper will install its own hooks only."
#endif

#ifndef UCOS2_THREAD_DEFAULT_STACK
#define UCOS2_THREAD_DEFAULT_STACK   512u
#endif

/*
 * Helper structure used to maintain intrusive lists of CMSIS objects. The wrapper
 * keeps lightweight tracking information to enable enumeration and cleanup.
 */
#define UCOS2_PRIORITY_LOWEST_AVAILABLE   (OS_LOWEST_PRIO - UCOS2_PRIORITY_GUARD)
#define UCOS2_PRIORITY_HIGHEST_AVAILABLE  (UCOS2_PRIORITY_LOWEST_AVAILABLE - (UCOS2_PRIORITY_LEVELS - 1u))

#if (UCOS2_PRIORITY_LOWEST_AVAILABLE < (UCOS2_PRIORITY_LEVELS - 1u))
#error "Not enough priority slots for CMSIS-RTOS2 mapping."
#endif

typedef struct os_ucos2_object {
  struct os_ucos2_object *prev;
  struct os_ucos2_object *next;
  const char            *name;
  uint32_t               attr_bits;
} os_ucos2_object_t;

typedef enum {
  osUcos2ObjectThread,
  osUcos2ObjectTimer,
  osUcos2ObjectEventFlags,
  osUcos2ObjectMutex,
  osUcos2ObjectSemaphore,
  osUcos2ObjectMemoryPool,
  osUcos2ObjectMessageQueue
} os_ucos2_object_type_t;

typedef struct os_ucos2_list {
  os_ucos2_object_t *head;
  os_ucos2_object_t *tail;
} os_ucos2_list_t;

typedef enum {
  osUcos2ThreadDetached = 0u,
  osUcos2ThreadJoinable = 1u
} os_ucos2_thread_mode_t;

typedef struct os_ucos2_thread {
  os_ucos2_object_t object;
  os_ucos2_object_type_t type;
  osThreadFunc_t    entry;
  void             *argument;
  OS_TCB           *tcb;
  OS_STK           *stack_mem;
  uint32_t          stack_size;
  INT8U             ucos_prio;
  osPriority_t      cmsis_prio;
  osThreadState_t   state;
  OS_EVENT         *join_sem;
  OS_FLAG_GRP      *flags_grp;
  uint32_t          flags_cached;
  os_ucos2_thread_mode_t mode;
  uint8_t           started;
  uint8_t           owns_cb_mem;
  uint8_t           owns_stack_mem;
  uint8_t           reserved[1];
} os_ucos2_thread_t;

typedef struct os_ucos2_timer {
  os_ucos2_object_t object;
  OS_TMR           *ostmr;
  osTimerFunc_t     callback;
  void             *argument;
  osTimerType_t     type;
} os_ucos2_timer_t;

typedef struct os_ucos2_event_flags {
  os_ucos2_object_t object;
  OS_FLAG_GRP      *grp;
} os_ucos2_event_flags_t;

typedef struct os_ucos2_mutex {
  os_ucos2_object_t object;
  OS_EVENT         *event;
  uint32_t          recursive;
} os_ucos2_mutex_t;

typedef struct os_ucos2_semaphore {
  os_ucos2_object_t object;
  OS_EVENT         *event;
  uint32_t          max_count;
} os_ucos2_semaphore_t;

typedef struct os_ucos2_memory_pool {
  os_ucos2_object_t object;
  OS_MEM           *mem;
  void             *pool_mem;
  uint32_t          block_size;
  uint32_t          block_count;
} os_ucos2_memory_pool_t;

typedef struct os_ucos2_message_queue {
  os_ucos2_object_t object;
  OS_EVENT         *queue_event;
  void            **queue_storage;
  os_ucos2_memory_pool_t data_pool;
  uint32_t          msg_size;
  uint32_t          msg_count;
} os_ucos2_message_queue_t;

/*
 * Kernel bookkeeping structure.
 */
typedef struct os_ucos2_kernel {
  osKernelState_t state;
  uint32_t        lock_nesting;
  uint32_t        tick_freq;
  uint32_t        sys_timer_freq;
  bool            initialized;
  os_ucos2_list_t threads;
} os_ucos2_kernel_t;

extern os_ucos2_kernel_t os_ucos2_kernel;

INT8U osUcos2PriorityEncode(osPriority_t priority);
osPriority_t osUcos2PriorityDecode(INT8U ucos_prio);
os_ucos2_thread_t *osUcos2ThreadFromId(osThreadId_t thread_id);
os_ucos2_thread_t *osUcos2ThreadFromTcb(const OS_TCB *ptcb);
void osUcos2ThreadListInsert(os_ucos2_thread_t *thread);
void osUcos2ThreadListRemove(os_ucos2_thread_t *thread);
void osUcos2ThreadCleanup(os_ucos2_thread_t *thread);
void osUcos2ThreadJoinRelease(os_ucos2_thread_t *thread);

#ifdef __cplusplus
}
#endif

#endif /* UCOS2_OS2_H_ */
