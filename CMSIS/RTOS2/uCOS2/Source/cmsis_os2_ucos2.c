static os_ucos2_event_flags_t *osUcos2EventFlagsFromId(osEventFlagsId_t ef_id) {
  if (ef_id == NULL) {
    return NULL;
  }
  os_ucos2_event_flags_t *ef = (os_ucos2_event_flags_t *)ef_id;
  return (ef->object.type == osUcos2ObjectEventFlags) ? ef : NULL;
}
#include <string.h>

#include "ucos2_os2.h"

os_ucos2_kernel_t os_ucos2_kernel = {
  .state        = osKernelInactive,
  .lock_nesting = 0u,
  .tick_freq    = OS_TICKS_PER_SEC,
  .sys_timer_freq = OS_TICKS_PER_SEC,
  .initialized  = false,
  .threads      = { NULL, NULL }
};

static inline bool osUcos2IrqContext(void) {
  return (OSIntNesting > 0u) ? true : false;
}

static inline bool osUcos2SchedulerStarted(void) {
  return (OSRunning == OS_TRUE);
}

static void osUcos2ObjectInit(os_ucos2_object_t *object,
                              os_ucos2_object_type_t type,
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

static int32_t osUcos2PriorityOrdinal(osPriority_t priority) {
  for (int32_t i = 0; i < OS_PRIORITY_LUT_COUNT; ++i) {
    if (os_priority_lut[i] == priority) {
      return i;
    }
  }
  return -1;
}

INT8U osUcos2PriorityEncode(osPriority_t priority) {
  int32_t ordinal = osUcos2PriorityOrdinal(priority);
  if (ordinal < 0) {
    ordinal = osUcos2PriorityOrdinal(osPriorityNormal);
  }

  int32_t ucos_prio = (int32_t)UCOS2_PRIORITY_LOWEST_AVAILABLE - ordinal;
  if (ucos_prio < 0) {
    ucos_prio = UCOS2_PRIORITY_LOWEST_AVAILABLE;
  }
  return (INT8U)ucos_prio;
}

osPriority_t osUcos2PriorityDecode(INT8U ucos_prio) {
  int32_t ordinal = (int32_t)UCOS2_PRIORITY_LOWEST_AVAILABLE - (int32_t)ucos_prio;
  if ((ordinal < 0) || (ordinal >= OS_PRIORITY_LUT_COUNT)) {
    return osPriorityError;
  }

  return os_priority_lut[ordinal];
}

/* ==== Thread bookkeeping helpers ==== */

os_ucos2_thread_t *osUcos2ThreadFromId(osThreadId_t thread_id) {
  if (thread_id == NULL) {
    return NULL;
  }

  os_ucos2_thread_t *thread = (os_ucos2_thread_t *)thread_id;
  if (thread->object.type != osUcos2ObjectThread) {
    return NULL;
  }

  return thread;
}

os_ucos2_thread_t *osUcos2ThreadFromTcb(const OS_TCB *ptcb) {
  if (ptcb == NULL) {
    return NULL;
  }

  os_ucos2_object_t *cursor = os_ucos2_kernel.threads.head;
  while (cursor != NULL) {
    os_ucos2_thread_t *thread = (os_ucos2_thread_t *)cursor;
    if (thread->tcb == ptcb) {
      return thread;
    }
    cursor = cursor->next;
  }

  return NULL;
}

static void osUcos2ObjectListInsert(os_ucos2_list_t *list, os_ucos2_object_t *object) {
  object->prev = list->tail;
  object->next = NULL;

  if (list->tail != NULL) {
    list->tail->next = object;
  } else {
    list->head = object;
  }

  list->tail = object;
}

static void osUcos2ObjectListRemove(os_ucos2_list_t *list, os_ucos2_object_t *object) {
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

void osUcos2ThreadListInsert(os_ucos2_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  osUcos2ObjectListInsert(&os_ucos2_kernel.threads, &thread->object);
}

void osUcos2ThreadListRemove(os_ucos2_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  osUcos2ObjectListRemove(&os_ucos2_kernel.threads, &thread->object);
}

void osUcos2ThreadJoinRelease(os_ucos2_thread_t *thread) {
  if ((thread == NULL) || (thread->join_sem == NULL)) {
    return;
  }

  thread->tcb = NULL;
  thread->state = osThreadTerminated;
  (void)OSSemPost(thread->join_sem);
}

static os_ucos2_mutex_t *osUcos2MutexFromId(osMutexId_t mutex_id) {
  if (mutex_id == NULL) {
    return NULL;
  }
  os_ucos2_mutex_t *mutex = (os_ucos2_mutex_t *)mutex_id;
  return (mutex->object.type == osUcos2ObjectMutex) ? mutex : NULL;
}

static os_ucos2_semaphore_t *osUcos2SemaphoreFromId(osSemaphoreId_t semaphore_id) {
  if (semaphore_id == NULL) {
    return NULL;
  }
  os_ucos2_semaphore_t *sem = (os_ucos2_semaphore_t *)semaphore_id;
  return (sem->object.type == osUcos2ObjectSemaphore) ? sem : NULL;
}

static os_ucos2_timer_t *osUcos2TimerFromId(osTimerId_t timer_id) {
  if (timer_id == NULL) {
    return NULL;
  }
  os_ucos2_timer_t *timer = (os_ucos2_timer_t *)timer_id;
  return (timer->object.type == osUcos2ObjectTimer) ? timer : NULL;
}

static os_ucos2_message_queue_t *osUcos2MessageQueueFromId(osMessageQueueId_t mq_id) {
  if (mq_id == NULL) {
    return NULL;
  }
  os_ucos2_message_queue_t *mq = (os_ucos2_message_queue_t *)mq_id;
  return (mq->object.type == osUcos2ObjectMessageQueue) ? mq : NULL;
}

static void osUcos2ThreadFreeResources(os_ucos2_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  INT8U err;
  if (thread->join_sem != NULL) {
    (void)OSSemDel(thread->join_sem, OS_DEL_ALWAYS, &err);
    thread->join_sem = NULL;
  }
}

void osUcos2ThreadCleanup(os_ucos2_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  osUcos2ThreadListRemove(thread);

  if ((thread->mode == osUcos2ThreadJoinable) && (thread->join_sem != NULL)) {
    thread->tcb = NULL;
    thread->state = osThreadTerminated;
    return;
  }

  osUcos2ThreadFreeResources(thread);
}

static os_ucos2_thread_t *osUcos2ThreadAlloc(const osThreadAttr_t *attr) {
  if ((attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos2_thread_t))) {
    return NULL;
  }

  os_ucos2_thread_t *thread = (os_ucos2_thread_t *)attr->cb_mem;
  memset(thread, 0, sizeof(*thread));
  thread->owns_cb_mem = 0u;
  osUcos2ObjectInit(&thread->object,
                    osUcos2ObjectThread,
                    attr->name,
                    attr->attr_bits);
  return thread;
}

static uint32_t osUcos2StackWords(uint32_t stack_size_bytes) {
  uint32_t bytes = (stack_size_bytes != 0u) ? stack_size_bytes : UCOS2_THREAD_DEFAULT_STACK;
  uint32_t words = (bytes + sizeof(OS_STK) - 1u) / sizeof(OS_STK);

  if (words < 64u) {
    words = 64u;
  }
  return words;
}

static INT8U osUcos2AllocatePriority(osPriority_t priority) {
  INT8U desired = osUcos2PriorityEncode(priority);
  INT8U prio = desired;

  while (true) {
    if (OSTCBPrioTbl[prio] == (OS_TCB *)0) {
      return prio;
    }

    if (prio <= UCOS2_PRIORITY_HIGHEST_AVAILABLE) {
      break;
    }
    prio--;
  }

  return OS_PRIO_SELF;
}

static void osUcos2ThreadTrampoline(void *argument) {
  os_ucos2_thread_t *thread = (os_ucos2_thread_t *)argument;

  if (thread == NULL) {
    OSTaskDel(OS_PRIO_SELF);
  }

  thread->state = osThreadRunning;
  thread->entry(thread->argument);

  osThreadExit();
}

/* ==== Kernel Management ==== */

osStatus_t osKernelInitialize(void) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  if (os_ucos2_kernel.state != osKernelInactive) {
    return osError;
  }

  OSInit();
  os_ucos2_kernel.initialized = true;
  os_ucos2_kernel.state       = osKernelReady;

  return osOK;
}

osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size) {
  if (version != NULL) {
    version->api    = 0x02020000u;                   /* CMSIS-RTOS2 v2.2.0 */
    version->kernel = ((uint32_t)OS_VERSION) << 8;   /* Encode uCOS-II version */
  }

  if ((id_buf != NULL) && (id_size != 0u)) {
    static const char kernel_id[] = "uC/OS-II CMSIS-RTOS2";
    uint32_t copy = (id_size < sizeof(kernel_id)) ? id_size : sizeof(kernel_id);
    memcpy(id_buf, kernel_id, copy);
  }

  return osOK;
}

osKernelState_t osKernelGetState(void) {
  if (osUcos2SchedulerStarted()) {
    return osKernelRunning;
  }

  if (!os_ucos2_kernel.initialized) {
    return osKernelInactive;
  }

  return os_ucos2_kernel.state;
}

osStatus_t osKernelStart(void) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  if (!os_ucos2_kernel.initialized || (os_ucos2_kernel.state != osKernelReady)) {
    return osError;
  }

  os_ucos2_kernel.state = osKernelRunning;
  OSStart();
  return osOK;
}

int32_t osKernelLock(void) {
  if (osUcos2IrqContext()) {
    return (int32_t)osErrorISR;
  }

  if (!osUcos2SchedulerStarted()) {
    return (int32_t)osError;
  }

  int32_t previous = (OSLockNesting > 0u) ? 1 : 0;
  OSSchedLock();
  return previous;
}

int32_t osKernelUnlock(void) {
  if (osUcos2IrqContext()) {
    return (int32_t)osErrorISR;
  }

  if (!osUcos2SchedulerStarted()) {
    return (int32_t)osError;
  }

  int32_t previous = (OSLockNesting > 0u) ? 1 : 0;
  OSSchedUnlock();
  return previous;
}

int32_t osKernelRestoreLock(int32_t lock) {
  if (osUcos2IrqContext()) {
    return (int32_t)osErrorISR;
  }

  if (!osUcos2SchedulerStarted()) {
    return (int32_t)osError;
  }

  if (lock > 0) {
    OSSchedLock();
  } else {
    while (OSLockNesting > 0u) {
      OSSchedUnlock();
    }
  }

  return (OSLockNesting > 0u) ? 1 : 0;
}

uint32_t osKernelGetTickCount(void) {
  return OSTimeGet();
}

uint32_t osKernelGetTickFreq(void) {
  return os_ucos2_kernel.tick_freq;
}

uint32_t osKernelGetSysTimerCount(void) {
  return OSTimeGet();
}

uint32_t osKernelGetSysTimerFreq(void) {
  return os_ucos2_kernel.sys_timer_freq;
}

/* ==== Thread Management ==== */

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr) {
  if ((func == NULL) || (attr == NULL) ||
      (attr->stack_mem == NULL) || (attr->stack_size == 0u)) {
    return NULL;
  }

  if (osUcos2IrqContext()) {
    return NULL;
  }

  if (!os_ucos2_kernel.initialized) {
    return NULL;
  }

  osPriority_t priority = (attr != NULL && attr->priority != osPriorityNone)
                          ? attr->priority
                          : osPriorityNormal;

  INT8U ucos_prio = osUcos2AllocatePriority(priority);
  if (ucos_prio == OS_PRIO_SELF) {
    return NULL;
  }

  os_ucos2_thread_t *thread = osUcos2ThreadAlloc(attr);
  if (thread == NULL) {
    return NULL;
  }

  uint32_t stack_words = osUcos2StackWords(attr->stack_size);
  OS_STK *stack_mem = (OS_STK *)attr->stack_mem;
  thread->owns_stack_mem = 0u;
  thread->stack_mem = stack_mem;
  thread->stack_size = stack_words * sizeof(OS_STK);
  thread->entry = func;
  thread->argument = argument;
  thread->cmsis_prio = priority;
  thread->ucos_prio = ucos_prio;
  thread->state = osThreadReady;
  thread->join_sem = NULL;
  thread->mode = osUcos2ThreadDetached;

  if ((thread->object.attr_bits & osThreadJoinable) != 0u) {
    thread->mode = osUcos2ThreadJoinable;
    thread->join_sem = OSSemCreate(0u);
    if (thread->join_sem == NULL) {
      osUcos2ThreadFreeResources(thread);
      return NULL;
    }
  } else {
    thread->mode = osUcos2ThreadDetached;
  }

  if ((thread->mode == osUcos2ThreadJoinable) && (thread->join_sem == NULL)) {
    osUcos2ThreadFreeResources(thread);
    return NULL;
  }

  osUcos2ThreadListInsert(thread);

#if OS_STK_GROWTH == 1u
  OS_STK *ptos = &stack_mem[stack_words - 1u];
  OS_STK *pbos = stack_mem;
#else
  OS_STK *ptos = stack_mem;
  OS_STK *pbos = &stack_mem[stack_words - 1u];
#endif

  INT8U err = OSTaskCreateExt(osUcos2ThreadTrampoline,
                        thread,
                        ptos,
                        ucos_prio,
                        (INT16U)ucos_prio,
                        pbos,
                        stack_words,
                        thread,
                        OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR);
  if (err != OS_ERR_NONE) {
    thread->mode = osUcos2ThreadDetached;
    osUcos2ThreadCleanup(thread);
    return NULL;
  }

  thread->tcb = OSTCBPrioTbl[ucos_prio];
  thread->started = 1u;
  return (osThreadId_t)thread;
}

const char *osThreadGetName(osThreadId_t thread_id) {
  os_ucos2_thread_t *thread = (thread_id == NULL)
                              ? osUcos2ThreadFromTcb(OSTCBCur)
                              : osUcos2ThreadFromId(thread_id);
  if (thread == NULL) {
    return NULL;
  }

  return thread->object.name;
}

osThreadId_t osThreadGetId(void) {
  if (!osUcos2SchedulerStarted()) {
    return NULL;
  }

  os_ucos2_thread_t *thread = osUcos2ThreadFromTcb(OSTCBCur);
  return (osThreadId_t)thread;
}

osThreadState_t osThreadGetState(osThreadId_t thread_id) {
  os_ucos2_thread_t *thread = osUcos2ThreadFromId(thread_id);
  if (thread == NULL) {
    return osThreadError;
  }

  if (thread->tcb == NULL) {
    return osThreadTerminated;
  }

  if (thread->tcb == OSTCBCur) {
    return osThreadRunning;
  }

  if (thread->tcb->OSTCBStat == OS_STAT_RDY) {
    return osThreadReady;
  }

  if ((thread->tcb->OSTCBStat & (OS_STAT_PEND_ANY | OS_STAT_SUSPEND)) != 0u) {
    return osThreadBlocked;
  }

  return osThreadReady;
}

osPriority_t osThreadGetPriority(osThreadId_t thread_id) {
  os_ucos2_thread_t *thread = osUcos2ThreadFromId(thread_id);
  if (thread == NULL) {
    return osPriorityError;
  }

  return thread->cmsis_prio;
}

osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority) {
  os_ucos2_thread_t *thread = osUcos2ThreadFromId(thread_id);
  if (thread == NULL) {
    return osErrorParameter;
  }

  if (priority == osPriorityNone) {
    return osErrorParameter;
  }

  if (thread->tcb == NULL) {
    return osErrorResource;
  }

  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  INT8U new_prio = osUcos2PriorityEncode(priority);
  INT8U err = OSTaskChangePrio(thread->ucos_prio, new_prio);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  thread->ucos_prio = new_prio;
  thread->cmsis_prio = priority;
  return osOK;
}

osStatus_t osThreadYield(void) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  if (!osUcos2SchedulerStarted()) {
    return osError;
  }

  OSSched();
  return osOK;
}

__NO_RETURN void osThreadExit(void) {
  if (osUcos2IrqContext()) {
    while (1) {
      /* no-op */
    }
  }

  os_ucos2_thread_t *thread = osUcos2ThreadFromTcb(OSTCBCur);
  osUcos2ThreadJoinRelease(thread);
  osUcos2ThreadCleanup(thread);

  (void)OSTaskDel(OS_PRIO_SELF);
  for (;;) {
    ;
  }
}

osStatus_t osThreadTerminate(osThreadId_t thread_id) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  os_ucos2_thread_t *thread = (thread_id == NULL)
                              ? osUcos2ThreadFromTcb(OSTCBCur)
                              : osUcos2ThreadFromId(thread_id);
  if (thread == NULL) {
    return osErrorParameter;
  }

  INT8U target_prio = (thread->tcb == OSTCBCur) ? OS_PRIO_SELF : thread->ucos_prio;
  INT8U err = OSTaskDel(target_prio);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  osUcos2ThreadJoinRelease(thread);
  osUcos2ThreadCleanup(thread);
  return osOK;
}

osStatus_t osThreadDetach(osThreadId_t thread_id) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  os_ucos2_thread_t *thread = osUcos2ThreadFromId(thread_id);
  if (thread == NULL) {
    return osErrorParameter;
  }

  if (thread->mode != osUcos2ThreadJoinable) {
    return osErrorResource;
  }

  INT8U err;
  if (thread->join_sem != NULL) {
    (void)OSSemDel(thread->join_sem, OS_DEL_ALWAYS, &err);
    thread->join_sem = NULL;
  }

  thread->mode = osUcos2ThreadDetached;

  if (thread->tcb == NULL) {
    osUcos2ThreadFreeResources(thread);
  }

  return osOK;
}

osStatus_t osThreadJoin(osThreadId_t thread_id) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  os_ucos2_thread_t *thread = osUcos2ThreadFromId(thread_id);
  if (thread == NULL) {
    return osErrorParameter;
  }

  if (thread->mode != osUcos2ThreadJoinable) {
    return osErrorResource;
  }

  if (thread == osUcos2ThreadFromTcb(OSTCBCur)) {
    return osErrorResource;
  }

  if (thread->join_sem == NULL) {
    osUcos2ThreadFreeResources(thread);
    return osOK;
  }

  INT8U err;
  OSSemPend(thread->join_sem, 0u, &err);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  osUcos2ThreadFreeResources(thread);
  return osOK;
}

/* ==== Generic Wait ==== */

osStatus_t osDelay(uint32_t ticks) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  if (!osUcos2SchedulerStarted()) {
    return osError;
  }

  if (ticks == 0u) {
    OSSched();
    return osOK;
  }

  OSTimeDly(ticks);
  return osOK;
}

osStatus_t osDelayUntil(uint32_t ticks) {
  if (osUcos2IrqContext()) {
    return osErrorISR;
  }

  if (!osUcos2SchedulerStarted()) {
    return osError;
  }

  uint32_t now = OSTimeGet();
  if (ticks <= now) {
    return osErrorParameter;
  }

  return osDelay(ticks - now);
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

static osStatus_t osUcos2MutexError(INT8U err) {
  switch (err) {
    case OS_ERR_NONE:
      return osOK;
    case OS_ERR_TIMEOUT:
      return osErrorTimeout;
    case OS_ERR_PEND_ABORT:
    case OS_ERR_NOT_MUTEX_OWNER:
    case OS_ERR_EVENT_TYPE:
    case OS_ERR_PCP_LOWER:
      return osErrorResource;
    case OS_ERR_PEND_ISR:
      return osErrorISR;
    default:
      return osError;
  }
}

osMutexId_t osMutexNew(const osMutexAttr_t *attr) {
  if ((attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos2_mutex_t))) {
    return NULL;
  }

  if ((attr->attr_bits & osMutexRecursive) != 0u) {
    return NULL;
  }

  os_ucos2_mutex_t *mutex = (os_ucos2_mutex_t *)attr->cb_mem;
  memset(mutex, 0, sizeof(*mutex));
  osUcos2ObjectInit(&mutex->object, osUcos2ObjectMutex, attr->name, attr->attr_bits);

  INT8U err;
  mutex->event = OSMutexCreate(OS_PRIO_MUTEX_CEIL_DIS, &err);
  if (err != OS_ERR_NONE) {
    return NULL;
  }

  return (osMutexId_t)mutex;
}

const char *osMutexGetName(osMutexId_t mutex_id) {
  os_ucos2_mutex_t *mutex = osUcos2MutexFromId(mutex_id);
  return (mutex != NULL) ? mutex->object.name : NULL;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout) {
  os_ucos2_mutex_t *mutex = osUcos2MutexFromId(mutex_id);
  if (mutex == NULL) {
    return osErrorParameter;
  }

  INT32U pend_timeout = (timeout == osWaitForever) ? 0u : timeout;
  INT8U err;
  (void)OSMutexPend(mutex->event, pend_timeout, &err);
  return osUcos2MutexError(err);
}

osStatus_t osMutexRelease(osMutexId_t mutex_id) {
  os_ucos2_mutex_t *mutex = osUcos2MutexFromId(mutex_id);
  if (mutex == NULL) {
    return osErrorParameter;
  }

  INT8U err = OSMutexPost(mutex->event);
  return osUcos2MutexError(err);
}

osThreadId_t osMutexGetOwner(osMutexId_t mutex_id) {
  os_ucos2_mutex_t *mutex = osUcos2MutexFromId(mutex_id);
  if (mutex == NULL) {
    return NULL;
  }

  OS_EVENT *event = mutex->event;
  if ((event == NULL) || (event->OSEventPtr == NULL)) {
    return NULL;
  }

  return (osThreadId_t)osUcos2ThreadFromTcb((OS_TCB *)event->OSEventPtr);
}

osStatus_t osMutexDelete(osMutexId_t mutex_id) {
  os_ucos2_mutex_t *mutex = osUcos2MutexFromId(mutex_id);
  if (mutex == NULL) {
    return osErrorParameter;
  }

  INT8U err;
  (void)OSMutexDel(mutex->event, OS_DEL_ALWAYS, &err);
  mutex->event = NULL;
  return osUcos2MutexError(err);
}

/* ==== Semaphore Management ==== */

static osStatus_t osUcos2SemaphoreError(INT8U err) {
  switch (err) {
    case OS_ERR_NONE:
      return osOK;
    case OS_ERR_TIMEOUT:
      return osErrorTimeout;
    case OS_ERR_PEND_ISR:
      return osErrorISR;
    case OS_ERR_PEND_ABORT:
    case OS_ERR_SEM_OVF:
    case OS_ERR_EVENT_TYPE:
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
      (attr->cb_size < sizeof(os_ucos2_semaphore_t)) ||
      (max_count == 0u) ||
      (initial_count > max_count)) {
    return NULL;
  }

  os_ucos2_semaphore_t *sem = (os_ucos2_semaphore_t *)attr->cb_mem;
  memset(sem, 0, sizeof(*sem));
  osUcos2ObjectInit(&sem->object, osUcos2ObjectSemaphore, attr->name, attr->attr_bits);

  INT8U err;
  sem->event = OSSemCreate((INT16U)initial_count);
  if (sem->event == NULL) {
    return NULL;
  }

  sem->max_count = max_count;
  sem->initial_count = initial_count;
  return (osSemaphoreId_t)sem;
}

const char *osSemaphoreGetName(osSemaphoreId_t semaphore_id) {
  os_ucos2_semaphore_t *sem = osUcos2SemaphoreFromId(semaphore_id);
  return (sem != NULL) ? sem->object.name : NULL;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout) {
  os_ucos2_semaphore_t *sem = osUcos2SemaphoreFromId(semaphore_id);
  if (sem == NULL) {
    return osErrorParameter;
  }

  INT32U pend_timeout = (timeout == osWaitForever) ? 0u : timeout;
  INT8U err;
  OSSemPend(sem->event, pend_timeout, &err);
  return osUcos2SemaphoreError(err);
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id) {
  os_ucos2_semaphore_t *sem = osUcos2SemaphoreFromId(semaphore_id);
  if (sem == NULL) {
    return osErrorParameter;
  }

  INT8U err = OSSemPost(sem->event);
  return osUcos2SemaphoreError(err);
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id) {
  os_ucos2_semaphore_t *sem = osUcos2SemaphoreFromId(semaphore_id);
  if (sem == NULL) {
    return 0u;
  }

  OS_SEM_DATA data;
  INT8U err = OSSemQuery(sem->event, &data);
  if (err != OS_ERR_NONE) {
    return 0u;
  }

  return (uint32_t)data.OSCnt;
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id) {
  os_ucos2_semaphore_t *sem = osUcos2SemaphoreFromId(semaphore_id);
  if (sem == NULL) {
    return osErrorParameter;
  }

  INT8U err;
  (void)OSSemDel(sem->event, OS_DEL_ALWAYS, &err);
  sem->event = NULL;
  return osUcos2SemaphoreError(err);
}

/* ==== Timer Management ==== */

static void osUcos2TimerThunk(void *ptmr, void *parg) {
  (void)ptmr;
  os_ucos2_timer_t *timer = (os_ucos2_timer_t *)parg;
  if ((timer != NULL) && (timer->callback != NULL)) {
    timer->callback(timer->argument);
  }

  if ((timer != NULL) && (timer->type == osTimerOnce)) {
    timer->active = 0u;
  }
}

static osStatus_t osUcos2TimerDeleteInternal(os_ucos2_timer_t *timer) {
  if (timer == NULL) {
    return osErrorParameter;
  }

  if (timer->ostmr != NULL) {
    INT8U err;
    (void)OSTmrStop(timer->ostmr, OS_TMR_OPT_NONE, NULL, &err);
    (void)OSTmrDel(timer->ostmr, &err);
    timer->ostmr = NULL;
  }
  timer->active = 0u;
  return osOK;
}

osTimerId_t osTimerNew(osTimerFunc_t func,
                       osTimerType_t type,
                       void *argument,
                       const osTimerAttr_t *attr) {
  if ((func == NULL) || (attr == NULL) ||
      (attr->cb_mem == NULL) ||
      (attr->cb_size < sizeof(os_ucos2_timer_t))) {
    return NULL;
  }

  os_ucos2_timer_t *timer = (os_ucos2_timer_t *)attr->cb_mem;
  memset(timer, 0, sizeof(*timer));
  osUcos2ObjectInit(&timer->object, osUcos2ObjectTimer, attr->name, attr->attr_bits);
  timer->callback = func;
  timer->argument = argument;
  timer->type = type;
  timer->ostmr = NULL;
  timer->active = 0u;

  return (osTimerId_t)timer;
}

const char *osTimerGetName(osTimerId_t timer_id) {
  os_ucos2_timer_t *timer = osUcos2TimerFromId(timer_id);
  return (timer != NULL) ? timer->object.name : NULL;
}

static osStatus_t osUcos2TimerCreateInstance(os_ucos2_timer_t *timer, uint32_t ticks) {
  if ((timer == NULL) || (ticks == 0u)) {
    return osErrorParameter;
  }

  INT8U opt = (timer->type == osTimerPeriodic) ? OS_TMR_OPT_PERIODIC : OS_TMR_OPT_ONE_SHOT;
  INT32U period = (timer->type == osTimerPeriodic) ? ticks : 0u;
  INT8U err;

  OS_TMR *ostmr = OSTmrCreate(ticks,
                              period,
                              opt,
                              osUcos2TimerThunk,
                              timer,
                              (INT8U *)(void *)((timer->object.name != NULL) ? timer->object.name : "?"),
                              &err);
  if (err != OS_ERR_NONE) {
    return osErrorResource;
  }

  timer->ostmr = ostmr;
  return osOK;
}

osStatus_t osTimerStart(osTimerId_t timer_id, uint32_t ticks) {
  os_ucos2_timer_t *timer = osUcos2TimerFromId(timer_id);
  if (timer == NULL) {
    return osErrorParameter;
  }

  if (timer->ostmr != NULL) {
    osUcos2TimerDeleteInternal(timer);
  }

  osStatus_t stat = osUcos2TimerCreateInstance(timer, ticks);
  if (stat != osOK) {
    return stat;
  }

  INT8U err;
  if (OSTmrStart(timer->ostmr, &err) != OS_TRUE) {
    osUcos2TimerDeleteInternal(timer);
    return osErrorResource;
  }

  timer->active = 1u;
  return osOK;
}

osStatus_t osTimerStop(osTimerId_t timer_id) {
  os_ucos2_timer_t *timer = osUcos2TimerFromId(timer_id);
  if ((timer == NULL) || (timer->ostmr == NULL)) {
    return osErrorResource;
  }
  osStatus_t stat = osUcos2TimerDeleteInternal(timer);
  return stat;
}

uint32_t osTimerIsRunning(osTimerId_t timer_id) {
  os_ucos2_timer_t *timer = osUcos2TimerFromId(timer_id);
  if ((timer == NULL) || (timer->ostmr == NULL)) {
    return 0u;
  }

  INT8U err;
  INT8U state = OSTmrStateGet(timer->ostmr, &err);
  if (err != OS_ERR_NONE) {
    return 0u;
  }

  return (state == OS_TMR_STATE_RUNNING) ? 1u : 0u;
}

osStatus_t osTimerDelete(osTimerId_t timer_id) {
  os_ucos2_timer_t *timer = osUcos2TimerFromId(timer_id);
  if (timer == NULL) {
    return osErrorParameter;
  }

  return osUcos2TimerDeleteInternal(timer);
}

/* ==== Message Queue Management ==== */

static osStatus_t osUcos2MessageQueueError(INT8U err) {
  switch (err) {
    case OS_ERR_NONE:
      return osOK;
    case OS_ERR_TIMEOUT:
      return osErrorTimeout;
    case OS_ERR_PEND_ISR:
      return osErrorISR;
    case OS_ERR_Q_EMPTY:
    case OS_ERR_Q_FULL:
    case OS_ERR_PEND_ABORT:
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
      (attr->cb_size < sizeof(os_ucos2_message_queue_t)) ||
      (attr->mq_mem == NULL) ||
      (attr->mq_size < (msg_count * sizeof(void *)))) {
    return NULL;
  }

  os_ucos2_message_queue_t *mq = (os_ucos2_message_queue_t *)attr->cb_mem;
  memset(mq, 0, sizeof(*mq));
  osUcos2ObjectInit(&mq->object, osUcos2ObjectMessageQueue, attr->name, attr->attr_bits);

  void **storage = (void **)attr->mq_mem;
  INT8U err;
  mq->queue_event = OSQCreate(storage, (INT16U)msg_count);
  if (mq->queue_event == NULL) {
    return NULL;
  }

  mq->space_sem = OSSemCreate((INT16U)msg_count);
  if (mq->space_sem == NULL) {
    (void)OSQDel(mq->queue_event, OS_DEL_ALWAYS, &err);
    mq->queue_event = NULL;
    return NULL;
  }

  mq->queue_storage = storage;
  mq->msg_count = msg_count;
  mq->msg_size = msg_size;
  return (osMessageQueueId_t)mq;
}

const char *osMessageQueueGetName(osMessageQueueId_t mq_id) {
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  return (mq != NULL) ? mq->object.name : NULL;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id,
                             const void *msg_ptr,
                             uint8_t msg_prio,
                             uint32_t timeout) {
  (void)msg_prio;
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  if ((mq == NULL) || (msg_ptr == NULL)) {
    return osErrorParameter;
  }

  void *message = *(void * const *)msg_ptr;
  INT32U pend_timeout = (timeout == osWaitForever) ? 0u : timeout;
  INT8U err;

  OSSemPend(mq->space_sem, pend_timeout, &err);
  if (err != OS_ERR_NONE) {
    return osUcos2MessageQueueError(err);
  }

  err = OSQPost(mq->queue_event, message);
  if (err != OS_ERR_NONE) {
    (void)OSSemPost(mq->space_sem);
    return osUcos2MessageQueueError(err);
  }

  return osOK;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id,
                             void *msg_ptr,
                             uint8_t *msg_prio,
                             uint32_t timeout) {
  (void)msg_prio;
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  if ((mq == NULL) || (msg_ptr == NULL)) {
    return osErrorParameter;
  }

  INT32U pend_timeout = (timeout == osWaitForever) ? 0u : timeout;
  INT8U err;
  void *message = OSQPend(mq->queue_event, pend_timeout, &err);
  if (err != OS_ERR_NONE) {
    return osUcos2MessageQueueError(err);
  }

  (void)OSSemPost(mq->space_sem);
  *(void **)msg_ptr = message;
  return osOK;
}

uint32_t osMessageQueueGetCapacity(osMessageQueueId_t mq_id) {
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  return (mq != NULL) ? mq->msg_count : 0u;
}

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id) {
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  return (mq != NULL) ? mq->msg_size : 0u;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id) {
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  if (mq == NULL) {
    return 0u;
  }

  OS_Q_DATA data;
  INT8U err = OSQQuery(mq->queue_event, &data);
  if (err != OS_ERR_NONE) {
    return 0u;
  }

  return (uint32_t)data.OSNMsgs;
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id) {
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  if (mq == NULL) {
    return 0u;
  }

  OS_SEM_DATA data;
  INT8U err = OSSemQuery(mq->space_sem, &data);
  if (err != OS_ERR_NONE) {
    return 0u;
  }

  return (uint32_t)data.OSCnt;
}

osStatus_t osMessageQueueReset(osMessageQueueId_t mq_id) {
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  if (mq == NULL) {
    return osErrorParameter;
  }

  INT8U err;
  (void)OSQFlush(mq->queue_event);
  OSSemSet(mq->space_sem, (INT16U)mq->msg_count, &err);
  return (err == OS_ERR_NONE) ? osOK : osErrorResource;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id) {
  os_ucos2_message_queue_t *mq = osUcos2MessageQueueFromId(mq_id);
  if (mq == NULL) {
    return osErrorParameter;
  }

  INT8U err;
  (void)OSQDel(mq->queue_event, OS_DEL_ALWAYS, &err);
  if (err != OS_ERR_NONE) {
    return osUcos2MessageQueueError(err);
  }
  mq->queue_event = NULL;

  (void)OSSemDel(mq->space_sem, OS_DEL_ALWAYS, &err);
  mq->space_sem = NULL;
  return osUcos2MessageQueueError(err);
}
