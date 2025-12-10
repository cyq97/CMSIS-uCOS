#ifndef UCOS3_OS2_H_
#define UCOS3_OS2_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "os.h"

/*
 * Configuration validation: uC/OS-III allows most services to be disabled at
 * build time. The CMSIS-RTOS2 wrapper depends on several core options – fail
 * early if the project configuration is incompatible.
 */
#if (OS_CFG_TASK_DEL_EN != DEF_ENABLED)
#error "Enable OS_CFG_TASK_DEL_EN for CMSIS thread lifecycle control."
#endif

#if (OS_CFG_TASK_SUSPEND_EN != DEF_ENABLED)
#error "Enable OS_CFG_TASK_SUSPEND_EN for osThreadSuspend/osThreadResume."
#endif

#if (OS_CFG_MUTEX_EN != DEF_ENABLED)
#error "Enable OS_CFG_MUTEX_EN to support the CMSIS mutex API."
#endif

#if (OS_CFG_SEM_EN != DEF_ENABLED)
#error "Enable OS_CFG_SEM_EN to support the CMSIS semaphore API."
#endif

#if (OS_CFG_Q_EN != DEF_ENABLED)
#error "Enable OS_CFG_Q_EN to support CMSIS message queues."
#endif

#if (OS_CFG_FLAG_EN != DEF_ENABLED)
#error "Enable OS_CFG_FLAG_EN to support CMSIS event flags."
#endif

#if (OS_CFG_TMR_EN != DEF_ENABLED)
#error "Enable OS_CFG_TMR_EN to support CMSIS timers."
#endif

#ifndef OS_CFG_PRIO_MAX
#error "OS_CFG_PRIO_MAX must be defined in os_cfg.h."
#endif

#define UCOS3_PRIORITY_LEVELS          50u
#define UCOS3_PRIORITY_GUARD           4u   /* Reserve Idle/Stat/Timer/Start task slots */

#if (OS_CFG_PRIO_MAX < (UCOS3_PRIORITY_LEVELS + UCOS3_PRIORITY_GUARD + 1u))
#error "Increase OS_CFG_PRIO_MAX to accommodate CMSIS-RTOS2 priority levels."
#endif

#ifndef UCOS3_THREAD_DEFAULT_STACK
#define UCOS3_THREAD_DEFAULT_STACK   512u
#endif

#define UCOS3_PRIORITY_LOWEST_AVAILABLE  (OS_CFG_PRIO_MAX - 1u - UCOS3_PRIORITY_GUARD)
#define UCOS3_PRIORITY_HIGHEST_AVAILABLE (UCOS3_PRIORITY_LOWEST_AVAILABLE - (UCOS3_PRIORITY_LEVELS - 1u))

#if (UCOS3_PRIORITY_HIGHEST_AVAILABLE < 2u)
#error "Not enough priority slots left for CMSIS-RTOS2 mapping."
#endif

typedef enum {
  osUcos3ObjectThread,
  osUcos3ObjectTimer,
  osUcos3ObjectEventFlags,
  osUcos3ObjectMutex,
  osUcos3ObjectSemaphore,
  osUcos3ObjectMemoryPool,
  osUcos3ObjectMessageQueue
} os_ucos3_object_type_t;

typedef struct os_ucos3_object {
  struct os_ucos3_object *prev;
  struct os_ucos3_object *next;
  const char             *name;
  uint32_t                attr_bits;
  os_ucos3_object_type_t  type;
} os_ucos3_object_t;

typedef struct os_ucos3_list {
  os_ucos3_object_t *head;
  os_ucos3_object_t *tail;
} os_ucos3_list_t;

typedef enum {
  osUcos3ThreadDetached = 0u,
  osUcos3ThreadJoinable = 1u
} os_ucos3_thread_mode_t;

typedef struct os_ucos3_thread {
  os_ucos3_object_t   object;
  osThreadFunc_t      entry;
  void               *argument;
  OS_TCB              tcb;
  CPU_STK            *stack_mem;
  uint32_t            stack_size;
  OS_PRIO             ucos_prio;
  osPriority_t        cmsis_prio;
  osThreadState_t     state;
  os_ucos3_thread_mode_t mode;
  OS_SEM              join_sem;
  bool                join_sem_created;
  bool                started;
} os_ucos3_thread_t;

typedef struct os_ucos3_timer {
  os_ucos3_object_t object;
  OS_TMR            timer;
  osTimerFunc_t     callback;
  void             *argument;
  osTimerType_t     type;
  bool              active;
} os_ucos3_timer_t;

typedef struct os_ucos3_event_flags {
  os_ucos3_object_t object;
  OS_FLAG_GRP       grp;
  bool              created;
} os_ucos3_event_flags_t;

typedef struct os_ucos3_mutex {
  os_ucos3_object_t object;
  OS_MUTEX          mutex;
  bool              created;
} os_ucos3_mutex_t;

typedef struct os_ucos3_semaphore {
  os_ucos3_object_t object;
  OS_SEM            sem;
  OS_SEM_CTR        max_count;
  OS_SEM_CTR        initial_count;
  bool              created;
} os_ucos3_semaphore_t;

typedef struct os_ucos3_memory_pool {
  os_ucos3_object_t object;
  uint8_t          *pool_mem;
  uint16_t         *free_stack;
  uint32_t          block_size;
  uint32_t          block_count;
  uint32_t          free_top;
  uint8_t           owns_memory;
} os_ucos3_memory_pool_t;

typedef struct os_ucos3_message_queue {
  os_ucos3_object_t object;
  OS_Q              queue;
  uint32_t          msg_size;
  uint32_t          msg_count;
  bool              created;
} os_ucos3_message_queue_t;

typedef struct os_ucos3_kernel {
  osKernelState_t state;
  uint32_t        tick_freq;
  uint32_t        sys_timer_freq;
  bool            initialized;
  os_ucos3_list_t threads;
} os_ucos3_kernel_t;

extern os_ucos3_kernel_t os_ucos3_kernel;

OS_PRIO     osUcos3PriorityEncode(osPriority_t priority);
osPriority_t osUcos3PriorityDecode(OS_PRIO ucos_prio);
os_ucos3_thread_t *osUcos3ThreadFromId(osThreadId_t thread_id);
os_ucos3_thread_t *osUcos3ThreadFromTcb(const OS_TCB *ptcb);
void osUcos3ThreadListInsert(os_ucos3_thread_t *thread);
void osUcos3ThreadListRemove(os_ucos3_thread_t *thread);
void osUcos3ThreadCleanup(os_ucos3_thread_t *thread);
void osUcos3ThreadJoinRelease(os_ucos3_thread_t *thread);
os_ucos3_event_flags_t *osUcos3EventFlagsFromId(osEventFlagsId_t ef_id);
os_ucos3_mutex_t *osUcos3MutexFromId(osMutexId_t mutex_id);
os_ucos3_semaphore_t *osUcos3SemaphoreFromId(osSemaphoreId_t semaphore_id);
os_ucos3_message_queue_t *osUcos3MessageQueueFromId(osMessageQueueId_t mq_id);

#ifdef __cplusplus
}
#endif

#endif /* UCOS3_OS2_H_ */
