#ifndef OS_CFG_APP_H
#define OS_CFG_APP_H
/* Minimal stub for syntax-only compile checks. */

/* Stack sizes required by uC/OS-III public header declarations. */
#define OS_CFG_IDLE_TASK_STK_SIZE  64u
#define OS_CFG_ISR_STK_SIZE        64u

/* Message pool and timer task stack declarations referenced by os.h. */
#define OS_CFG_MSG_POOL_SIZE       32u
#define OS_CFG_TMR_TASK_STK_SIZE   128u

/* Timer task rate (Hz) must be > 0 when timers are enabled. */
#define OS_CFG_TMR_TASK_RATE_HZ    100u

#endif
