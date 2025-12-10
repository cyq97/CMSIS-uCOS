#include <string.h>

#include "ucos3_os2.h"

#ifndef UCOS3_THREAD_MIN_STACK_WORDS
#define UCOS3_THREAD_MIN_STACK_WORDS   96u
#endif

os_ucos3_kernel_t os_ucos3_kernel = {
  .state         = osKernelInactive,
  .tick_freq     = OS_CFG_TICK_RATE_HZ,
  .sys_timer_freq = OS_CFG_TICK_RATE_HZ,
  .initialized   = false,
  .threads       = { NULL, NULL }
};

static inline bool osUcos3IrqContext(void) {
  return (OSIntNestingCtr > (OS_NESTING_CTR)0u);
}

static inline bool osUcos3SchedulerRunning(void) {
  return (OSRunning == OS_STATE_OS_RUNNING);
}

static void osUcos3ObjectInit(os_ucos3_object_t *object,
                              os_ucos3_object_type_t type,
                              const char *name,
                              uint32_t attr_bits) {
  if (object == NULL) {
    return;
  }

  object->prev = NULL;
  object->next = NULL;
  object->name = name;
  object->attr_bits = attr_bits;
  object->type = type;
}

static const uint32_t os_ucos3_flag_mask = (uint32_t)((OS_FLAGS)-1);

static bool osUcos3FlagsValid(uint32_t flags) {
  if (flags == 0u) {
    return false;
  }
  return ((flags & ~os_ucos3_flag_mask) == 0u);
}

static bool osUcos3FlagsOptionsValid(uint32_t options) {
  const uint32_t allowed = osFlagsWaitAll | osFlagsNoClear;
  return ((options & ~allowed) == 0u);
}

static OS_OPT osUcos3FlagsPendOptions(uint32_t options, uint32_t timeout) {
  OS_OPT opt = (options & osFlagsWaitAll) != 0u ? OS_OPT_PEND_FLAG_SET_ALL
                                               : OS_OPT_PEND_FLAG_SET_ANY;
  if ((options & osFlagsNoClear) == 0u) {
    opt |= OS_OPT_PEND_FLAG_CONSUME;
  }

  if (timeout == 0u) {
    opt |= OS_OPT_PEND_NON_BLOCKING;
  } else {
    opt |= OS_OPT_PEND_BLOCKING;
  }

  return opt;
}

static uint32_t osUcos3EventFlagsError(OS_ERR err) {
  switch (err) {
    case OS_ERR_NONE:
      return 0u;
    case OS_ERR_TIMEOUT:
      return osFlagsErrorTimeout;
    case OS_ERR_PEND_ABORT:
    case OS_ERR_OBJ_DEL:
      return osFlagsErrorResource;
    case OS_ERR_PEND_ISR:
      return osFlagsErrorISR;
    case OS_ERR_PEND_WOULD_BLOCK:
    case OS_ERR_OBJ_PTR_NULL:
    case OS_ERR_OBJ_TYPE:
    case OS_ERR_FLAG_INVALID_PEND_OPT:
    case OS_ERR_FLAG_INVALID:
      return osFlagsErrorParameter;
    default:
      return osFlagsErrorUnknown;
  }
}

/* ==== Priority Mapping Helpers ==== */

static const osPriority_t os_priority_lut[] = {
  osPriorityIdle,
  osPriorityLow,        osPriorityLow1,        osPriorityLow2,        osPriorityLow3,
  osPriorityLow4,       osPriorityLow5,        osPriorityLow6,        osPriorityLow7,
  osPriorityBelowNormal,  osPriorityBelowNormal1, osPriorityBelowNormal2, osPriorityBelowNormal3,
  osPriorityBelowNormal4, osPriorityBelowNormal5, osPriorityBelowNormal6, osPriorityBelowNormal7,
  osPriorityNormal,     osPriorityNormal1,     osPriorityNormal2,     osPriorityNormal3,
  osPriorityNormal4,    osPriorityNormal5,     osPriorityNormal6,     osPriorityNormal7,
  osPriorityAboveNormal,  osPriorityAboveNormal1, osPriorityAboveNormal2, osPriorityAboveNormal3,
  osPriorityAboveNormal4, osPriorityAboveNormal5, osPriorityAboveNormal6, osPriorityAboveNormal7,
  osPriorityHigh,       osPriorityHigh1,       osPriorityHigh2,       osPriorityHigh3,
  osPriorityHigh4,      osPriorityHigh5,       osPriorityHigh6,       osPriorityHigh7,
  osPriorityRealtime,   osPriorityRealtime1,   osPriorityRealtime2,   osPriorityRealtime3,
  osPriorityRealtime4,  osPriorityRealtime5,   osPriorityRealtime6,   osPriorityRealtime7,
  osPriorityISR
};

#define OS_PRIORITY_LUT_COUNT   ((int32_t)(sizeof(os_priority_lut) / sizeof(os_priority_lut[0])))

static int32_t osUcos3PriorityOrdinal(osPriority_t priority) {
  for (int32_t i = 0; i < OS_PRIORITY_LUT_COUNT; ++i) {
    if (os_priority_lut[i] == priority) {
      return i;
    }
  }
  return -1;
}

OS_PRIO osUcos3PriorityEncode(osPriority_t priority) {
  int32_t ordinal = osUcos3PriorityOrdinal(priority);
  if (ordinal < 0) {
    ordinal = osUcos3PriorityOrdinal(osPriorityNormal);
  }

  int32_t ucos_prio = (int32_t)UCOS3_PRIORITY_LOWEST_AVAILABLE - ordinal;
  if (ucos_prio < (int32_t)UCOS3_PRIORITY_HIGHEST_AVAILABLE) {
    ucos_prio = (int32_t)UCOS3_PRIORITY_HIGHEST_AVAILABLE;
  }
  if (ucos_prio > (int32_t)UCOS3_PRIORITY_LOWEST_AVAILABLE) {
    ucos_prio = (int32_t)UCOS3_PRIORITY_LOWEST_AVAILABLE;
  }

  return (OS_PRIO)ucos_prio;
}

osPriority_t osUcos3PriorityDecode(OS_PRIO ucos_prio) {
  int32_t ordinal = (int32_t)UCOS3_PRIORITY_LOWEST_AVAILABLE - (int32_t)ucos_prio;
  if ((ordinal < 0) || (ordinal >= OS_PRIORITY_LUT_COUNT)) {
    return osPriorityError;
  }

  return os_priority_lut[ordinal];
}

/* ==== Thread bookkeeping helpers ==== */

os_ucos3_thread_t *osUcos3ThreadFromId(osThreadId_t thread_id) {
  if (thread_id == NULL) {
    return NULL;
  }

  os_ucos3_thread_t *thread = (os_ucos3_thread_t *)thread_id;
  return (thread->object.type == osUcos3ObjectThread) ? thread : NULL;
}

os_ucos3_thread_t *osUcos3ThreadFromTcb(const OS_TCB *ptcb) {
  if (ptcb == NULL) {
    return NULL;
  }

  os_ucos3_object_t *cursor = os_ucos3_kernel.threads.head;
  while (cursor != NULL) {
    os_ucos3_thread_t *thread = (os_ucos3_thread_t *)cursor;
    if (&thread->tcb == ptcb) {
      return thread;
    }
    cursor = cursor->next;
  }

  return NULL;
}

os_ucos3_event_flags_t *osUcos3EventFlagsFromId(osEventFlagsId_t ef_id) {
  if (ef_id == NULL) {
    return NULL;
  }

  os_ucos3_event_flags_t *ef = (os_ucos3_event_flags_t *)ef_id;
  return (ef->object.type == osUcos3ObjectEventFlags) ? ef : NULL;
}

os_ucos3_mutex_t *osUcos3MutexFromId(osMutexId_t mutex_id) {
  if (mutex_id == NULL) {
    return NULL;
  }

  os_ucos3_mutex_t *mutex = (os_ucos3_mutex_t *)mutex_id;
  return (mutex->object.type == osUcos3ObjectMutex) ? mutex : NULL;
}

os_ucos3_semaphore_t *osUcos3SemaphoreFromId(osSemaphoreId_t semaphore_id) {
  if (semaphore_id == NULL) {
    return NULL;
  }

  os_ucos3_semaphore_t *sem = (os_ucos3_semaphore_t *)semaphore_id;
  return (sem->object.type == osUcos3ObjectSemaphore) ? sem : NULL;
}

static os_ucos3_timer_t *osUcos3TimerFromId(osTimerId_t timer_id) {
  if (timer_id == NULL) {
    return NULL;
  }

  os_ucos3_timer_t *timer = (os_ucos3_timer_t *)timer_id;
  return (timer->object.type == osUcos3ObjectTimer) ? timer : NULL;
}

os_ucos3_message_queue_t *osUcos3MessageQueueFromId(osMessageQueueId_t mq_id) {
  if (mq_id == NULL) {
    return NULL;
  }

  os_ucos3_message_queue_t *mq = (os_ucos3_message_queue_t *)mq_id;
  return (mq->object.type == osUcos3ObjectMessageQueue) ? mq : NULL;
}

static void osUcos3ObjectListInsert(os_ucos3_list_t *list, os_ucos3_object_t *object) {
  object->prev = list->tail;
  object->next = NULL;

  if (list->tail != NULL) {
    list->tail->next = object;
  } else {
    list->head = object;
  }

  list->tail = object;
}

static void osUcos3ObjectListRemove(os_ucos3_list_t *list, os_ucos3_object_t *object) {
  if (object->prev != NULL) {
    object->prev->next = object->next;
  } else {
    list->head = object->next;
  }

  if (object->next != NULL) {
    object->next->prev = object->prev;
  } else {
    list->tail = object->prev;
  }

  object->prev = NULL;
  object->next = NULL;
}

void osUcos3ThreadListInsert(os_ucos3_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  osUcos3ObjectListInsert(&os_ucos3_kernel.threads, &thread->object);
}

void osUcos3ThreadListRemove(os_ucos3_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  osUcos3ObjectListRemove(&os_ucos3_kernel.threads, &thread->object);
}

void osUcos3ThreadJoinRelease(os_ucos3_thread_t *thread) {
  if ((thread == NULL) || (thread->mode != osUcos3ThreadJoinable) || !thread->join_sem_created) {
    return;
  }

  OS_ERR err;
  (void)OSSemPost(&thread->join_sem, OS_OPT_POST_1, &err);
}

static void osUcos3ThreadFreeResources(os_ucos3_thread_t *thread) {
  if ((thread == NULL) || !thread->join_sem_created) {
    return;
  }

  OS_ERR err;
  (void)OSSemDel(&thread->join_sem, OS_OPT_DEL_ALWAYS, &err);
  thread->join_sem_created = false;
}

void osUcos3ThreadCleanup(os_ucos3_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  osUcos3ThreadListRemove(thread);
  thread->started = false;
  thread->state = osThreadTerminated;

  if (thread->mode == osUcos3ThreadDetached) {
    osUcos3ThreadFreeResources(thread);
  }
}

static os_ucos3_thread_t *osUcos3ThreadAlloc(const osThreadAttr_t *attr) {
  if ((attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos3_thread_t)) ||
      (attr->stack_mem == NULL) ||
      (attr->stack_size == 0u)) {
    return NULL;
  }

  os_ucos3_thread_t *thread = (os_ucos3_thread_t *)attr->cb_mem;
  memset(thread, 0, sizeof(*thread));
  osUcos3ObjectInit(&thread->object,
                    osUcos3ObjectThread,
                    attr->name,
                    attr->attr_bits);
  return thread;
}

static CPU_STK_SIZE osUcos3StackWords(uint32_t stack_size_bytes) {
  uint32_t bytes = (stack_size_bytes != 0u) ? stack_size_bytes : UCOS3_THREAD_DEFAULT_STACK;
  CPU_STK_SIZE words = (CPU_STK_SIZE)((bytes + sizeof(CPU_STK) - 1u) / sizeof(CPU_STK));
  if (words < UCOS3_THREAD_MIN_STACK_WORDS) {
    words = UCOS3_THREAD_MIN_STACK_WORDS;
  }
  return words;
}

static void osUcos3ThreadTrampoline(void *p_arg) {
  os_ucos3_thread_t *thread = (os_ucos3_thread_t *)p_arg;
  if (thread == NULL) {
    OS_ERR err;
    OSTaskDel(NULL, &err);
  }

  thread->state = osThreadRunning;
  thread->entry(thread->argument);

  osThreadExit();
}

/* ==== Kernel Management ==== */

osStatus_t osKernelInitialize(void) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  if (os_ucos3_kernel.state != osKernelInactive) {
    return osError;
  }

  OS_ERR err;
  OSInit(&err);
  if (err != OS_ERR_NONE) {
    return osError;
  }

  os_ucos3_kernel.initialized = true;
  os_ucos3_kernel.state = osKernelReady;
  os_ucos3_kernel.tick_freq = OS_CFG_TICK_RATE_HZ;
  os_ucos3_kernel.sys_timer_freq = OS_CFG_TICK_RATE_HZ;

  return osOK;
}

osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size) {
  if (version != NULL) {
    version->api    = 0x02020000u;
    version->kernel = ((uint32_t)OS_VERSION) << 16;
  }

  if ((id_buf != NULL) && (id_size != 0u)) {
    static const char kernel_id[] = "uC/OS-III CMSIS-RTOS2";
    uint32_t copy = (id_size < sizeof(kernel_id)) ? id_size : sizeof(kernel_id);
    memcpy(id_buf, kernel_id, copy);
  }

  return osOK;
}

osKernelState_t osKernelGetState(void) {
  if (osUcos3SchedulerRunning()) {
    return osKernelRunning;
  }

  if (!os_ucos3_kernel.initialized) {
    return osKernelInactive;
  }

  return os_ucos3_kernel.state;
}

osStatus_t osKernelStart(void) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  if (!os_ucos3_kernel.initialized || (os_ucos3_kernel.state != osKernelReady)) {
    return osError;
  }

  os_ucos3_kernel.state = osKernelRunning;
  OS_ERR err;
  OSStart(&err);
  return (err == OS_ERR_NONE) ? osOK : osError;
}

int32_t osKernelLock(void) {
  if (osUcos3IrqContext()) {
    return (int32_t)osErrorISR;
  }

  if (!osUcos3SchedulerRunning()) {
    return (int32_t)osError;
  }

  int32_t previous = (OSSchedLockNestingCtr > 0u) ? 1 : 0;
  OS_ERR err;
  OSSchedLock(&err);
  if (err != OS_ERR_NONE) {
    return (int32_t)osError;
  }
  return previous;
}

int32_t osKernelUnlock(void) {
  if (osUcos3IrqContext()) {
    return (int32_t)osErrorISR;
  }

  if (!osUcos3SchedulerRunning()) {
    return (int32_t)osError;
  }

  int32_t previous = (OSSchedLockNestingCtr > 0u) ? 1 : 0;
  if (OSSchedLockNestingCtr == 0u) {
    return 0;
  }

  OS_ERR err;
  OSSchedUnlock(&err);
  if (err != OS_ERR_NONE) {
    return (int32_t)osError;
  }

  return previous;
}

int32_t osKernelRestoreLock(int32_t lock) {
  if (osUcos3IrqContext()) {
    return (int32_t)osErrorISR;
  }

  if (!osUcos3SchedulerRunning()) {
    return (int32_t)osError;
  }

  OS_ERR err;
  if (lock > 0) {
    OSSchedLock(&err);
    if (err != OS_ERR_NONE) {
      return (int32_t)osError;
    }
  } else {
    while (OSSchedLockNestingCtr > 0u) {
      OSSchedUnlock(&err);
      if (err != OS_ERR_NONE) {
        return (int32_t)osError;
      }
    }
  }

  return (OSSchedLockNestingCtr > 0u) ? 1 : 0;
}

uint32_t osKernelGetTickCount(void) {
  return (uint32_t)OSTimeGet();
}

uint32_t osKernelGetTickFreq(void) {
  return os_ucos3_kernel.tick_freq;
}

uint32_t osKernelGetSysTimerCount(void) {
  return (uint32_t)OSTimeGet();
}

uint32_t osKernelGetSysTimerFreq(void) {
  return os_ucos3_kernel.sys_timer_freq;
}

/* ==== Thread Management ==== */

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr) {
  if ((func == NULL) || (attr == NULL)) {
    return NULL;
  }

  if (osUcos3IrqContext()) {
    return NULL;
  }

  if (!os_ucos3_kernel.initialized) {
    return NULL;
  }

  os_ucos3_thread_t *thread = osUcos3ThreadAlloc(attr);
  if (thread == NULL) {
    return NULL;
  }

  thread->entry = func;
  thread->argument = argument;
  thread->cmsis_prio = (attr->priority != osPriorityNone) ? attr->priority : osPriorityNormal;
  thread->ucos_prio = osUcos3PriorityEncode(thread->cmsis_prio);
  thread->stack_mem = (CPU_STK *)attr->stack_mem;

  CPU_STK_SIZE stack_words = osUcos3StackWords(attr->stack_size);
  thread->stack_size = (uint32_t)(stack_words * sizeof(CPU_STK));

  thread->mode = ((thread->object.attr_bits & osThreadJoinable) != 0u)
                 ? osUcos3ThreadJoinable
                 : osUcos3ThreadDetached;

  if (thread->mode == osUcos3ThreadJoinable) {
    OS_ERR err;
    OSSemCreate(&thread->join_sem,
               (CPU_CHAR *)"cmsis.join",
               (OS_SEM_CTR)0u,
               &err);
    if (err != OS_ERR_NONE) {
      return NULL;
    }
    thread->join_sem_created = true;
  }

  osUcos3ThreadListInsert(thread);

  OS_ERR err;
  OSTaskCreate(&thread->tcb,
               (CPU_CHAR *)(thread->object.name != NULL ? thread->object.name : "cmsis.task"),
               osUcos3ThreadTrampoline,
               thread,
               thread->ucos_prio,
               &thread->stack_mem[0],
               (CPU_STK_SIZE)(stack_words / 10u),
               stack_words,
               (OS_MSG_QTY)0u,
               (OS_TICK)0u,
               NULL,
               (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
               &err);
  if (err != OS_ERR_NONE) {
    osUcos3ThreadCleanup(thread);
    osUcos3ThreadFreeResources(thread);
    return NULL;
  }

  thread->state = osThreadReady;
  thread->started = true;
  return (osThreadId_t)thread;
}

const char *osThreadGetName(osThreadId_t thread_id) {
  os_ucos3_thread_t *thread = (thread_id == NULL)
                              ? osUcos3ThreadFromTcb(OSTCBCurPtr)
                              : osUcos3ThreadFromId(thread_id);
  return (thread != NULL) ? thread->object.name : NULL;
}

osThreadId_t osThreadGetId(void) {
  if (!osUcos3SchedulerRunning()) {
    return NULL;
  }

  return (osThreadId_t)osUcos3ThreadFromTcb(OSTCBCurPtr);
}

static osThreadState_t osUcos3ThreadStateFromTcb(const os_ucos3_thread_t *thread) {
  if ((thread == NULL) || !thread->started) {
    return osThreadTerminated;
  }

  if (&thread->tcb == OSTCBCurPtr) {
    return osThreadRunning;
  }

  switch (thread->tcb.TaskState) {
    case OS_TASK_STATE_RDY:
      return osThreadReady;
    case OS_TASK_STATE_DLY:
    case OS_TASK_STATE_PEND:
    case OS_TASK_STATE_PEND_TIMEOUT:
    case OS_TASK_STATE_PEND_SUSPENDED:
    case OS_TASK_STATE_DLY_SUSPENDED:
    case OS_TASK_STATE_SUSPENDED:
      return osThreadBlocked;
    case OS_TASK_STATE_DEL:
      return osThreadTerminated;
    default:
      return osThreadReady;
  }
}

osThreadState_t osThreadGetState(osThreadId_t thread_id) {
  os_ucos3_thread_t *thread = osUcos3ThreadFromId(thread_id);
  if (thread == NULL) {
    return osThreadError;
  }

  return osUcos3ThreadStateFromTcb(thread);
}

osPriority_t osThreadGetPriority(osThreadId_t thread_id) {
  os_ucos3_thread_t *thread = osUcos3ThreadFromId(thread_id);
  if (thread == NULL) {
    return osPriorityError;
  }

  return thread->cmsis_prio;
}

osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority) {
  os_ucos3_thread_t *thread = osUcos3ThreadFromId(thread_id);
  if ((thread == NULL) || (priority == osPriorityNone)) {
    return osErrorParameter;
  }

  if (!thread->started) {
    return osErrorResource;
  }

  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  OS_ERR err;
  OS_PRIO new_prio = osUcos3PriorityEncode(priority);
  OSTaskChangePrio(&thread->tcb, new_prio, &err);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  thread->ucos_prio = new_prio;
  thread->cmsis_prio = priority;
  return osOK;
}

osStatus_t osThreadYield(void) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  if (!osUcos3SchedulerRunning()) {
    return osError;
  }

  OS_ERR err;
  OSTaskYield(&err);
  return (err == OS_ERR_NONE) ? osOK : osError;
}

__NO_RETURN void osThreadExit(void) {
  os_ucos3_thread_t *thread = osUcos3ThreadFromTcb(OSTCBCurPtr);
  if (thread == NULL) {
    OS_ERR err;
    OSTaskDel(NULL, &err);
    for (;;) {
      ;
    }
  }

  osUcos3ThreadJoinRelease(thread);
  osUcos3ThreadCleanup(thread);

  OS_ERR err;
  OSTaskDel(NULL, &err);
  (void)err;
  for (;;) {
    ;
  }
}

osStatus_t osThreadTerminate(osThreadId_t thread_id) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  os_ucos3_thread_t *thread = (thread_id == NULL)
                              ? osUcos3ThreadFromTcb(OSTCBCurPtr)
                              : osUcos3ThreadFromId(thread_id);
  if ((thread == NULL) || !thread->started) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSTaskDel(&thread->tcb, &err);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  osUcos3ThreadJoinRelease(thread);
  osUcos3ThreadCleanup(thread);
  return osOK;
}

osStatus_t osThreadSuspend(osThreadId_t thread_id) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  os_ucos3_thread_t *thread = (thread_id == NULL)
                              ? osUcos3ThreadFromTcb(OSTCBCurPtr)
                              : osUcos3ThreadFromId(thread_id);
  if ((thread == NULL) || !thread->started) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSTaskSuspend(&thread->tcb, &err);
  return (err == OS_ERR_NONE) ? osOK : osErrorResource;
}

osStatus_t osThreadResume(osThreadId_t thread_id) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  os_ucos3_thread_t *thread = osUcos3ThreadFromId(thread_id);
  if ((thread == NULL) || !thread->started) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSTaskResume(&thread->tcb, &err);
  return (err == OS_ERR_NONE) ? osOK : osErrorResource;
}

osStatus_t osThreadDetach(osThreadId_t thread_id) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  os_ucos3_thread_t *thread = osUcos3ThreadFromId(thread_id);
  if ((thread == NULL) || (thread->mode != osUcos3ThreadJoinable)) {
    return osErrorParameter;
  }

  if (thread->join_sem_created) {
    OS_ERR err;
    OSSemDel(&thread->join_sem, OS_OPT_DEL_ALWAYS, &err);
    thread->join_sem_created = false;
  }

  thread->mode = osUcos3ThreadDetached;

  if (!thread->started) {
    osUcos3ThreadFreeResources(thread);
  }

  return osOK;
}

osStatus_t osThreadJoin(osThreadId_t thread_id) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  os_ucos3_thread_t *thread = osUcos3ThreadFromId(thread_id);
  if ((thread == NULL) || (thread->mode != osUcos3ThreadJoinable)) {
    return osErrorParameter;
  }

  if (!thread->join_sem_created) {
    return osOK;
  }

  if (&thread->tcb == OSTCBCurPtr) {
    return osErrorResource;
  }

  OS_ERR err;
  OSSemPend(&thread->join_sem,
            (OS_TICK)0u,
            OS_OPT_PEND_BLOCKING,
            NULL,
            &err);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  osUcos3ThreadFreeResources(thread);
  return osOK;
}

/* ==== Generic Wait ==== */

static osStatus_t osUcos3DelayTicks(uint32_t ticks) {
  OS_ERR err;
  if (ticks == 0u) {
    OSTimeDly(0u, OS_OPT_TIME_DLY, &err);
    return (err == OS_ERR_NONE) ? osOK : osError;
  }

  OSTimeDly((OS_TICK)ticks, OS_OPT_TIME_DLY, &err);
  return (err == OS_ERR_NONE) ? osOK : osError;
}

osStatus_t osDelay(uint32_t ticks) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  if (!osUcos3SchedulerRunning()) {
    return osError;
  }

  return osUcos3DelayTicks(ticks);
}

osStatus_t osDelayUntil(uint32_t ticks) {
  if (osUcos3IrqContext()) {
    return osErrorISR;
  }

  if (!osUcos3SchedulerRunning()) {
    return osError;
  }

  OS_TICK now = OSTimeGet();
  if (ticks <= now) {
    return osErrorParameter;
  }

  return osUcos3DelayTicks(ticks - now);
}

/* ==== Thread Flags (Not Supported) ==== */

uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags) {
  (void)thread_id;
  (void)flags;
  return osFlagsErrorUnsupported;
}

uint32_t osThreadFlagsClear(uint32_t flags) {
  (void)flags;
  return osFlagsErrorUnsupported;
}

uint32_t osThreadFlagsGet(void) {
  return osFlagsErrorUnsupported;
}

uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout) {
  (void)flags;
  (void)options;
  (void)timeout;
  return osFlagsErrorUnsupported;
}

/* ==== Mutex Management ==== */

static osStatus_t osUcos3MutexError(OS_ERR err) {
  switch (err) {
    case OS_ERR_NONE:
      return osOK;
    case OS_ERR_TIMEOUT:
      return osErrorTimeout;
    case OS_ERR_PEND_ABORT:
    case OS_ERR_OBJ_DEL:
    case OS_ERR_MUTEX_NOT_OWNER:
    case OS_ERR_MUTEX_OVF:
      return osErrorResource;
    case OS_ERR_PEND_ISR:
      return osErrorISR;
    case OS_ERR_PEND_WOULD_BLOCK:
      return osErrorResource;
    default:
      return osError;
  }
}

osMutexId_t osMutexNew(const osMutexAttr_t *attr) {
  if ((attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos3_mutex_t))) {
    return NULL;
  }

  if ((attr->attr_bits & osMutexRecursive) != 0u) {
    return NULL;
  }

  os_ucos3_mutex_t *mutex = (os_ucos3_mutex_t *)attr->cb_mem;
  memset(mutex, 0, sizeof(*mutex));
  osUcos3ObjectInit(&mutex->object, osUcos3ObjectMutex, attr->name, attr->attr_bits);

  OS_ERR err;
  OSMutexCreate(&mutex->mutex,
                (CPU_CHAR *)(attr->name != NULL ? attr->name : "cmsis.mutex"),
                &err);
  if (err != OS_ERR_NONE) {
    return NULL;
  }

  mutex->created = true;
  return (osMutexId_t)mutex;
}

const char *osMutexGetName(osMutexId_t mutex_id) {
  os_ucos3_mutex_t *mutex = osUcos3MutexFromId(mutex_id);
  return (mutex != NULL) ? mutex->object.name : NULL;
}

static OS_TICK osUcos3PendTimeout(uint32_t timeout) {
  if ((timeout == 0u) || (timeout == osWaitForever)) {
    return (OS_TICK)0u;
  }
  return (OS_TICK)timeout;
}

static OS_OPT osUcos3PendOption(uint32_t timeout) {
  return (timeout == 0u) ? OS_OPT_PEND_NON_BLOCKING : OS_OPT_PEND_BLOCKING;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout) {
  os_ucos3_mutex_t *mutex = osUcos3MutexFromId(mutex_id);
  if ((mutex == NULL) || !mutex->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSMutexPend(&mutex->mutex,
              osUcos3PendTimeout(timeout),
              osUcos3PendOption(timeout),
              NULL,
              &err);
  if ((timeout == 0u) && (err == OS_ERR_PEND_WOULD_BLOCK)) {
    return osErrorResource;
  }

  return osUcos3MutexError(err);
}

osStatus_t osMutexRelease(osMutexId_t mutex_id) {
  os_ucos3_mutex_t *mutex = osUcos3MutexFromId(mutex_id);
  if ((mutex == NULL) || !mutex->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSMutexPost(&mutex->mutex, OS_OPT_POST_NONE, &err);
  return osUcos3MutexError(err);
}

osThreadId_t osMutexGetOwner(osMutexId_t mutex_id) {
  os_ucos3_mutex_t *mutex = osUcos3MutexFromId(mutex_id);
  if ((mutex == NULL) || !mutex->created) {
    return NULL;
  }

  if (mutex->mutex.OwnerTCBPtr == NULL) {
    return NULL;
  }

  return (osThreadId_t)osUcos3ThreadFromTcb(mutex->mutex.OwnerTCBPtr);
}

osStatus_t osMutexDelete(osMutexId_t mutex_id) {
  os_ucos3_mutex_t *mutex = osUcos3MutexFromId(mutex_id);
  if ((mutex == NULL) || !mutex->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSMutexDel(&mutex->mutex, OS_OPT_DEL_ALWAYS, &err);
  mutex->created = false;
  return osUcos3MutexError(err);
}

/* ==== Semaphore Management ==== */

static osStatus_t osUcos3SemaphoreError(OS_ERR err) {
  switch (err) {
    case OS_ERR_NONE:
      return osOK;
    case OS_ERR_TIMEOUT:
      return osErrorTimeout;
    case OS_ERR_PEND_ISR:
      return osErrorISR;
    case OS_ERR_SEM_OVF:
    case OS_ERR_PEND_ABORT:
    case OS_ERR_OBJ_DEL:
      return osErrorResource;
    case OS_ERR_PEND_WOULD_BLOCK:
      return osErrorResource;
    default:
      return osError;
  }
}

osSemaphoreId_t osSemaphoreNew(uint32_t max_count,
                               uint32_t initial_count,
                               const osSemaphoreAttr_t *attr) {
  if ((attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos3_semaphore_t)) ||
      (max_count == 0u) ||
      (initial_count > max_count)) {
    return NULL;
  }

  os_ucos3_semaphore_t *sem = (os_ucos3_semaphore_t *)attr->cb_mem;
  memset(sem, 0, sizeof(*sem));
  osUcos3ObjectInit(&sem->object, osUcos3ObjectSemaphore, attr->name, attr->attr_bits);

  OS_ERR err;
  OSSemCreate(&sem->sem,
              (CPU_CHAR *)(attr->name != NULL ? attr->name : "cmsis.sem"),
              (OS_SEM_CTR)initial_count,
              &err);
  if (err != OS_ERR_NONE) {
    return NULL;
  }

  sem->max_count = (OS_SEM_CTR)max_count;
  sem->initial_count = (OS_SEM_CTR)initial_count;
  sem->created = true;
  return (osSemaphoreId_t)sem;
}

const char *osSemaphoreGetName(osSemaphoreId_t semaphore_id) {
  os_ucos3_semaphore_t *sem = osUcos3SemaphoreFromId(semaphore_id);
  return (sem != NULL) ? sem->object.name : NULL;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout) {
  os_ucos3_semaphore_t *sem = osUcos3SemaphoreFromId(semaphore_id);
  if ((sem == NULL) || !sem->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSSemPend(&sem->sem,
            osUcos3PendTimeout(timeout),
            osUcos3PendOption(timeout),
            NULL,
            &err);
  if ((timeout == 0u) && (err == OS_ERR_PEND_WOULD_BLOCK)) {
    return osErrorResource;
  }

  return osUcos3SemaphoreError(err);
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id) {
  os_ucos3_semaphore_t *sem = osUcos3SemaphoreFromId(semaphore_id);
  if ((sem == NULL) || !sem->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSSemPost(&sem->sem, OS_OPT_POST_1, &err);
  return osUcos3SemaphoreError(err);
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id) {
  os_ucos3_semaphore_t *sem = osUcos3SemaphoreFromId(semaphore_id);
  if ((sem == NULL) || !sem->created) {
    return 0u;
  }

  OS_ERR err;
  OS_SEM_CTR cnt = OSSemCntGet(&sem->sem, &err);
  return (err == OS_ERR_NONE) ? (uint32_t)cnt : 0u;
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id) {
  os_ucos3_semaphore_t *sem = osUcos3SemaphoreFromId(semaphore_id);
  if ((sem == NULL) || !sem->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSSemDel(&sem->sem, OS_OPT_DEL_ALWAYS, &err);
  sem->created = false;
  return osUcos3SemaphoreError(err);
}

/* ==== Timer Management ==== */

static void osUcos3TimerThunk(void *p_tmr, void *p_arg) {
  (void)p_tmr;
  os_ucos3_timer_t *timer = (os_ucos3_timer_t *)p_arg;
  if ((timer == NULL) || (timer->callback == NULL)) {
    return;
  }

  timer->callback(timer->argument);
  if (timer->type == osTimerOnce) {
    timer->active = false;
  }
}

osTimerId_t osTimerNew(osTimerFunc_t func,
                       osTimerType_t type,
                       void *argument,
                       const osTimerAttr_t *attr) {
  if ((func == NULL) || (attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos3_timer_t))) {
    return NULL;
  }

  os_ucos3_timer_t *timer = (os_ucos3_timer_t *)attr->cb_mem;
  memset(timer, 0, sizeof(*timer));
  osUcos3ObjectInit(&timer->object, osUcos3ObjectTimer, attr->name, attr->attr_bits);
  timer->callback = func;
  timer->argument = argument;
  timer->type = type;

  OS_ERR err;
  OSTmrCreate(&timer->timer,
              (CPU_CHAR *)(attr->name != NULL ? attr->name : "cmsis.timer"),
              (OS_TICK)1u,
              (OS_TICK)0u,
              (type == osTimerPeriodic) ? OS_OPT_TMR_PERIODIC : OS_OPT_TMR_ONE_SHOT,
              osUcos3TimerThunk,
              timer,
              &err);
  if (err != OS_ERR_NONE) {
    return NULL;
  }

  timer->active = false;
  return (osTimerId_t)timer;
}

const char *osTimerGetName(osTimerId_t timer_id) {
  os_ucos3_timer_t *timer = osUcos3TimerFromId(timer_id);
  return (timer != NULL) ? timer->object.name : NULL;
}

static osStatus_t osUcos3TimerConfigure(os_ucos3_timer_t *timer, uint32_t ticks) {
  if ((timer == NULL) || (ticks == 0u)) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSTmrSet(&timer->timer,
           (OS_TICK)ticks,
           (timer->type == osTimerPeriodic) ? (OS_TICK)ticks : (OS_TICK)0u,
           (timer->type == osTimerPeriodic) ? OS_OPT_TMR_PERIODIC : OS_OPT_TMR_ONE_SHOT,
           osUcos3TimerThunk,
           timer,
           &err);
  return (err == OS_ERR_NONE) ? osOK : osErrorResource;
}

osStatus_t osTimerStart(osTimerId_t timer_id, uint32_t ticks) {
  os_ucos3_timer_t *timer = osUcos3TimerFromId(timer_id);
  if (timer == NULL) {
    return osErrorParameter;
  }

  osStatus_t stat = osUcos3TimerConfigure(timer, ticks);
  if (stat != osOK) {
    return stat;
  }

  OS_ERR err;
  OSTmrStart(&timer->timer, &err);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  timer->active = true;
  return osOK;
}

osStatus_t osTimerStop(osTimerId_t timer_id) {
  os_ucos3_timer_t *timer = osUcos3TimerFromId(timer_id);
  if (timer == NULL) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSTmrStop(&timer->timer, OS_OPT_TMR_NONE, NULL, &err);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  timer->active = false;
  return osOK;
}

uint32_t osTimerIsRunning(osTimerId_t timer_id) {
  os_ucos3_timer_t *timer = osUcos3TimerFromId(timer_id);
  if (timer == NULL) {
    return 0u;
  }

  OS_ERR err;
  OS_TMR_STATE state = OSTmrStateGet(&timer->timer, &err);
  if (err != OS_ERR_NONE) {
    return 0u;
  }

  return (state == OS_TMR_STATE_RUNNING) ? 1u : 0u;
}

osStatus_t osTimerDelete(osTimerId_t timer_id) {
  os_ucos3_timer_t *timer = osUcos3TimerFromId(timer_id);
  if (timer == NULL) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSTmrDel(&timer->timer, &err);
  timer->active = false;
  return (err == OS_ERR_NONE) ? osOK : osErrorResource;
}

/* ==== Event Flags Management ==== */

osEventFlagsId_t osEventFlagsNew(const osEventFlagsAttr_t *attr) {
  if ((attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos3_event_flags_t))) {
    return NULL;
  }

  os_ucos3_event_flags_t *ef = (os_ucos3_event_flags_t *)attr->cb_mem;
  memset(ef, 0, sizeof(*ef));
  osUcos3ObjectInit(&ef->object, osUcos3ObjectEventFlags, attr->name, attr->attr_bits);

  OS_ERR err;
  OSFlagCreate(&ef->grp,
               (CPU_CHAR *)(attr->name != NULL ? attr->name : "cmsis.flags"),
               (OS_FLAGS)0u,
               &err);
  if (err != OS_ERR_NONE) {
    return NULL;
  }

  ef->created = true;
  return (osEventFlagsId_t)ef;
}

const char *osEventFlagsGetName(osEventFlagsId_t ef_id) {
  os_ucos3_event_flags_t *ef = osUcos3EventFlagsFromId(ef_id);
  return (ef != NULL) ? ef->object.name : NULL;
}

uint32_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags) {
  os_ucos3_event_flags_t *ef = osUcos3EventFlagsFromId(ef_id);
  if ((ef == NULL) || !ef->created || !osUcos3FlagsValid(flags)) {
    return osFlagsErrorParameter;
  }

  OS_ERR err;
  OS_FLAGS result = OSFlagPost(&ef->grp, (OS_FLAGS)flags, OS_OPT_POST_FLAG_SET, &err);
  return (err == OS_ERR_NONE) ? (uint32_t)result : osUcos3EventFlagsError(err);
}

uint32_t osEventFlagsClear(osEventFlagsId_t ef_id, uint32_t flags) {
  os_ucos3_event_flags_t *ef = osUcos3EventFlagsFromId(ef_id);
  if ((ef == NULL) || !ef->created || !osUcos3FlagsValid(flags)) {
    return osFlagsErrorParameter;
  }

  OS_ERR err;
  OS_FLAGS result = OSFlagPost(&ef->grp, (OS_FLAGS)flags, OS_OPT_POST_FLAG_CLR, &err);
  return (err == OS_ERR_NONE) ? (uint32_t)result : osUcos3EventFlagsError(err);
}

uint32_t osEventFlagsGet(osEventFlagsId_t ef_id) {
  os_ucos3_event_flags_t *ef = osUcos3EventFlagsFromId(ef_id);
  if ((ef == NULL) || !ef->created) {
    return osFlagsErrorParameter;
  }

  OS_ERR err;
  OS_FLAGS flags = OSFlagQuery(&ef->grp, &err);
  return (err == OS_ERR_NONE) ? (uint32_t)flags : osUcos3EventFlagsError(err);
}

uint32_t osEventFlagsWait(osEventFlagsId_t ef_id,
                          uint32_t flags,
                          uint32_t options,
                          uint32_t timeout) {
  os_ucos3_event_flags_t *ef = osUcos3EventFlagsFromId(ef_id);
  if ((ef == NULL) || !ef->created ||
      !osUcos3FlagsValid(flags) ||
      !osUcos3FlagsOptionsValid(options)) {
    return osFlagsErrorParameter;
  }

  OS_ERR err;
  OS_FLAGS result = OSFlagPend(&ef->grp,
                               (OS_FLAGS)flags,
                               osUcos3PendTimeout(timeout),
                               osUcos3FlagsPendOptions(options, timeout),
                               NULL,
                               &err);
  return (err == OS_ERR_NONE) ? (uint32_t)result : osUcos3EventFlagsError(err);
}

osStatus_t osEventFlagsDelete(osEventFlagsId_t ef_id) {
  os_ucos3_event_flags_t *ef = osUcos3EventFlagsFromId(ef_id);
  if ((ef == NULL) || !ef->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSFlagDel(&ef->grp, OS_OPT_DEL_ALWAYS, &err);
  ef->created = false;
  if ((err == OS_ERR_OBJ_PTR_NULL) || (err == OS_ERR_OBJ_TYPE)) {
    return osErrorParameter;
  }

  return (err == OS_ERR_NONE) ? osOK : osErrorResource;
}

/* ==== Message Queue Management ==== */

static osStatus_t osUcos3MessageQueueError(OS_ERR err) {
  switch (err) {
    case OS_ERR_NONE:
      return osOK;
    case OS_ERR_TIMEOUT:
      return osErrorTimeout;
    case OS_ERR_PEND_ISR:
      return osErrorISR;
    case OS_ERR_Q_FULL:
    case OS_ERR_Q_EMPTY:
    case OS_ERR_PEND_ABORT:
    case OS_ERR_OBJ_DEL:
      return osErrorResource;
    case OS_ERR_PEND_WOULD_BLOCK:
      return osErrorResource;
    default:
      return osError;
  }
}

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count,
                                     uint32_t msg_size,
                                     const osMessageQueueAttr_t *attr) {
  if ((msg_count == 0u) ||
      (msg_size != sizeof(void *)) ||
      (attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos3_message_queue_t))) {
    return NULL;
  }

  os_ucos3_message_queue_t *mq = (os_ucos3_message_queue_t *)attr->cb_mem;
  memset(mq, 0, sizeof(*mq));
  osUcos3ObjectInit(&mq->object, osUcos3ObjectMessageQueue, attr->name, attr->attr_bits);

  OS_ERR err;
  OSQCreate(&mq->queue,
            (CPU_CHAR *)(attr->name != NULL ? attr->name : "cmsis.mq"),
            (OS_MSG_QTY)msg_count,
            &err);
  if (err != OS_ERR_NONE) {
    return NULL;
  }

  OSSemCreate(&mq->space_sem,
              (CPU_CHAR *)"cmsis.mq.space",
              (OS_SEM_CTR)msg_count,
              &err);
  if (err != OS_ERR_NONE) {
    OSQDel(&mq->queue, OS_OPT_DEL_ALWAYS, &err);
    return NULL;
  }

  mq->space_sem_created = true;
  mq->created = true;
  mq->msg_size = msg_size;
  mq->msg_count = msg_count;
  return (osMessageQueueId_t)mq;
}

const char *osMessageQueueGetName(osMessageQueueId_t mq_id) {
  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  return (mq != NULL) ? mq->object.name : NULL;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id,
                             const void *msg_ptr,
                             uint8_t msg_prio,
                             uint32_t timeout) {
  (void)msg_prio;

  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  if ((mq == NULL) || !mq->created || (msg_ptr == NULL)) {
    return osErrorParameter;
  }

  void *message = *(void * const *)msg_ptr;

  OS_ERR err;
  OSSemPend(&mq->space_sem,
            osUcos3PendTimeout(timeout),
            osUcos3PendOption(timeout),
            NULL,
            &err);
  if ((timeout == 0u) && (err == OS_ERR_PEND_WOULD_BLOCK)) {
    return osErrorResource;
  }
  if (err != OS_ERR_NONE) {
    return osUcos3MessageQueueError(err);
  }

  OSQPost(&mq->queue,
          message,
          (OS_MSG_SIZE)sizeof(void *),
          OS_OPT_POST_FIFO,
          &err);
  if (err != OS_ERR_NONE) {
    OS_ERR sem_err;
    OSSemPost(&mq->space_sem, OS_OPT_POST_1, &sem_err);
    return osUcos3MessageQueueError(err);
  }

  return osOK;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id,
                             void *msg_ptr,
                             uint8_t *msg_prio,
                             uint32_t timeout) {
  (void)msg_prio;

  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  if ((mq == NULL) || !mq->created || (msg_ptr == NULL)) {
    return osErrorParameter;
  }

  OS_ERR err;
  OS_MSG_SIZE size;
  void *message = OSQPend(&mq->queue,
                          osUcos3PendTimeout(timeout),
                          osUcos3PendOption(timeout),
                          &size,
                          NULL,
                          &err);
  if ((timeout == 0u) && (err == OS_ERR_PEND_WOULD_BLOCK)) {
    return osErrorResource;
  }
  if (err != OS_ERR_NONE) {
    return osUcos3MessageQueueError(err);
  }

  *(void **)msg_ptr = message;

  OS_ERR post_err;
  OSSemPost(&mq->space_sem, OS_OPT_POST_1, &post_err);
  (void)post_err;

  return osOK;
}

uint32_t osMessageQueueGetCapacity(osMessageQueueId_t mq_id) {
  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  return (mq != NULL) ? mq->msg_count : 0u;
}

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id) {
  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  return (mq != NULL) ? mq->msg_size : 0u;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id) {
  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  if ((mq == NULL) || !mq->created) {
    return 0u;
  }

  OS_ERR err;
  OS_MSG_QTY qty = OSQMsgQtyGet(&mq->queue, &err);
  return (err == OS_ERR_NONE) ? (uint32_t)qty : 0u;
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id) {
  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  if ((mq == NULL) || !mq->created) {
    return 0u;
  }

  OS_ERR err;
  OS_SEM_CTR cnt = OSSemCntGet(&mq->space_sem, &err);
  return (err == OS_ERR_NONE) ? (uint32_t)cnt : 0u;
}

osStatus_t osMessageQueueReset(osMessageQueueId_t mq_id) {
  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  if ((mq == NULL) || !mq->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSQFlush(&mq->queue, &err);
  if (err != OS_ERR_NONE) {
    return osUcos3MessageQueueError(err);
  }

  OSSemSet(&mq->space_sem, (OS_SEM_CTR)mq->msg_count, &err);
  return (err == OS_ERR_NONE) ? osOK : osErrorResource;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id) {
  os_ucos3_message_queue_t *mq = osUcos3MessageQueueFromId(mq_id);
  if ((mq == NULL) || !mq->created) {
    return osErrorParameter;
  }

  OS_ERR err;
  OSQDel(&mq->queue, OS_OPT_DEL_ALWAYS, &err);
  if (err != OS_ERR_NONE) {
    return osUcos3MessageQueueError(err);
  }

  if (mq->space_sem_created) {
    OSSemDel(&mq->space_sem, OS_OPT_DEL_ALWAYS, &err);
    mq->space_sem_created = false;
  }

  mq->created = false;
  return osOK;
}
