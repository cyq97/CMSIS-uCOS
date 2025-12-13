#ifndef APP_CFG_H
#define APP_CFG_H

/* Minimal application config stub for syntax-only compile checks. */

#define OS_MAX_TASKS            16u
#define OS_TASK_IDLE_STK_SIZE   64u
#define OS_TASK_TMR_STK_SIZE    128u

/* Timer wheel configuration used by uC/OS-II timer module. */
#define OS_TMR_CFG_MAX          8u
#define OS_TMR_CFG_WHEEL_SIZE   8u

#endif /* APP_CFG_H */
