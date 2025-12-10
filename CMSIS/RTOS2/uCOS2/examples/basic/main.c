#include "cmsis_os2.h"
#include "ucos2_os2.h"

/* 线程控制块与栈 */
static uint8_t producer_cb[sizeof(os_ucos2_thread_t)];
static uint8_t consumer_cb[sizeof(os_ucos2_thread_t)];
static uint8_t producer_stack[512];
static uint8_t consumer_stack[512];

/* 互斥量、信号量、消息队列、事件旗标、定时器控制块 */
static uint8_t mutex_cb[sizeof(os_ucos2_mutex_t)];
static uint8_t sem_cb[sizeof(os_ucos2_semaphore_t)];
static uint8_t mq_cb[sizeof(os_ucos2_message_queue_t)];
static void   *mq_storage[4];
static uint8_t flags_cb[sizeof(os_ucos2_event_flags_t)];
static uint8_t timer_cb[sizeof(os_ucos2_timer_t)];

/* CMSIS 对象句柄 */
static osThreadId_t producer_id;
static osThreadId_t consumer_id;
static osMutexId_t  mutex_id;
static osSemaphoreId_t sem_id;
static osMessageQueueId_t mq_id;
static osEventFlagsId_t flags_id;
static osTimerId_t timer_id;

/* 示例消息 */
typedef struct {
  uint32_t value;
} msg_t;

static msg_t messages[4];
static uint32_t msg_write_idx = 0;

static void producer_thread(void *argument) {
  (void)argument;
  for (;;) {
    osMutexAcquire(mutex_id, osWaitForever);
    msg_t *slot = &messages[msg_write_idx++ & 0x3u];
    slot->value++;
    osMutexRelease(mutex_id);

    osSemaphoreRelease(sem_id);
    osMessageQueuePut(mq_id, &slot, 0, osWaitForever);
    osDelay(10);
  }
}

static void consumer_thread(void *argument) {
  (void)argument;
  for (;;) {
    osSemaphoreAcquire(sem_id, osWaitForever);
    msg_t *slot = NULL;
    osMessageQueueGet(mq_id, &slot, NULL, osWaitForever);
    (void)slot;

    (void)osEventFlagsWait(flags_id, 0x1u, osFlagsWaitAny, osWaitForever);
  }
}

static void timer_callback(void *argument) {
  (void)argument;
  osEventFlagsSet(flags_id, 0x1u);
}

int main(void) {
  osKernelInitialize();

  /* 互斥量 */
  const osMutexAttr_t mutex_attr = {
    .name = "lock",
    .cb_mem = mutex_cb,
    .cb_size = sizeof(mutex_cb),
  };
  mutex_id = osMutexNew(&mutex_attr);

  /* 信号量 */
  const osSemaphoreAttr_t sem_attr = {
    .name = "sem",
    .cb_mem = sem_cb,
    .cb_size = sizeof(sem_cb),
  };
  sem_id = osSemaphoreNew(4, 0, &sem_attr);

  /* 消息队列 (指针消息) */
  const osMessageQueueAttr_t mq_attr = {
    .name = "queue",
    .cb_mem = mq_cb,
    .cb_size = sizeof(mq_cb),
    .mq_mem = mq_storage,
    .mq_size = sizeof(mq_storage),
  };
  mq_id = osMessageQueueNew(4, sizeof(void *), &mq_attr);

  /* 事件旗标 */
  const osEventFlagsAttr_t flags_attr = {
    .name = "flags",
    .cb_mem = flags_cb,
    .cb_size = sizeof(flags_cb),
  };
  flags_id = osEventFlagsNew(&flags_attr);

  /* 软件定时器 */
  const osTimerAttr_t timer_attr = {
    .name = "timer",
    .cb_mem = timer_cb,
    .cb_size = sizeof(timer_cb),
  };
  timer_id = osTimerNew(timer_callback, osTimerPeriodic, NULL, &timer_attr);
  osTimerStart(timer_id, 100);

  /* 线程 */
  const osThreadAttr_t producer_attr = {
    .name = "producer",
    .cb_mem = producer_cb,
    .cb_size = sizeof(producer_cb),
    .stack_mem = producer_stack,
    .stack_size = sizeof(producer_stack),
    .priority = osPriorityNormal,
  };
  const osThreadAttr_t consumer_attr = {
    .name = "consumer",
    .cb_mem = consumer_cb,
    .cb_size = sizeof(consumer_cb),
    .stack_mem = consumer_stack,
    .stack_size = sizeof(consumer_stack),
    .priority = osPriorityAboveNormal,
  };

  producer_id = osThreadNew(producer_thread, NULL, &producer_attr);
  consumer_id = osThreadNew(consumer_thread, NULL, &consumer_attr);

  osKernelStart();

  for (;;) {
  }
}
