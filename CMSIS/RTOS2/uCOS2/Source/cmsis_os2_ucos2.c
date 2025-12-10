#include <string.h>

#include "ucos2_os2.h"

os_ucos2_kernel_t os_ucos2_kernel = {
  .state       = osKernelInactive,
  .lock_nesting= 0u,
  .tick_freq   = OS_TICKS_PER_SEC,
  .sys_timer_freq = OS_TICKS_PER_SEC,
  .initialized = false
};

static inline bool osUcos2IrqContext(void) {
  return (OSIntNesting > 0u) ? true : false;
}

static inline bool osUcos2SchedulerStarted(void) {
  return (OSRunning == OS_TRUE);
}

/*
 * Kernel Management
 */
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
