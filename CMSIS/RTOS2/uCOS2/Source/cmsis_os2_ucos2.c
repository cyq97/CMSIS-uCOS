#include <string.h>
#include <stdlib.h>

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
  if (thread->type != osUcos2ObjectThread) {
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

static void osUcos2ThreadFreeResources(os_ucos2_thread_t *thread) {
  if (thread == NULL) {
    return;
  }

  INT8U err;
  if (thread->flags_grp != NULL) {
    (void)OSFlagDel(thread->flags_grp, OS_DEL_ALWAYS, &err);
    thread->flags_grp = NULL;
  }

  if (thread->join_sem != NULL) {
    (void)OSSemDel(thread->join_sem, OS_DEL_ALWAYS, &err);
    thread->join_sem = NULL;
  }

  if (thread->owns_stack_mem && (thread->stack_mem != NULL)) {
    free(thread->stack_mem);
    thread->stack_mem = NULL;
  }

  if (thread->owns_cb_mem) {
    free(thread);
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
  os_ucos2_thread_t *thread;

  if ((attr != NULL) && (attr->cb_mem != NULL) && (attr->cb_size >= sizeof(*thread))) {
    thread = (os_ucos2_thread_t *)attr->cb_mem;
    memset(thread, 0, sizeof(*thread));
    thread->owns_cb_mem = 0u;
  } else {
    thread = (os_ucos2_thread_t *)calloc(1, sizeof(*thread));
    if (thread == NULL) {
      return NULL;
    }
    thread->owns_cb_mem = 1u;
  }

  thread->object.prev = NULL;
  thread->object.next = NULL;
  thread->object.attr_bits = (attr != NULL) ? attr->attr_bits : 0u;
  thread->object.name = (attr != NULL) ? attr->name : NULL;
  thread->type = osUcos2ObjectThread;
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
  if (func == NULL) {
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

  uint32_t stack_words = osUcos2StackWords((attr != NULL) ? attr->stack_size : 0u);
  OS_STK *stack_mem = (attr != NULL && attr->stack_mem != NULL)
                      ? (OS_STK *)attr->stack_mem
                      : (OS_STK *)malloc(stack_words * sizeof(OS_STK));
  if (stack_mem == NULL) {
    if (thread->owns_cb_mem) {
      free(thread);
    }
    return NULL;
  }

  thread->owns_stack_mem = (attr == NULL || attr->stack_mem == NULL) ? 1u : 0u;
  thread->stack_mem = stack_mem;
  thread->stack_size = stack_words * sizeof(OS_STK);
  thread->entry = func;
  thread->argument = argument;
  thread->cmsis_prio = priority;
  thread->ucos_prio = ucos_prio;
  thread->state = osThreadReady;
  thread->flags_grp = NULL;
  thread->join_sem = NULL;

  INT8U err;
  thread->flags_grp = OSFlagCreate(0u, &err);
  if ((thread->flags_grp == NULL) || (err != OS_ERR_NONE)) {
    osUcos2ThreadFreeResources(thread);
    return NULL;
  }

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

  err = OSTaskCreateExt(osUcos2ThreadTrampoline,
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
