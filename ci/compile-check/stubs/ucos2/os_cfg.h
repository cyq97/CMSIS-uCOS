#ifndef OS_CFG_H
#define OS_CFG_H

/* Minimal configuration for syntax-only compile checks. */

#define OS_TASK_CREATE_EN          1u
#define OS_TASK_CREATE_EXT_EN      1u
#define OS_TASK_NAME_EN            1u
#define OS_TASK_DEL_EN             1u
#define OS_TASK_CHANGE_PRIO_EN     1u
#define OS_TASK_SUSPEND_EN         1u
#define OS_TASK_QUERY_EN           0u
#define OS_TASK_REG_TBL_SIZE       0u
#define OS_TASK_PROFILE_EN         0u
#define OS_TASK_SW_HOOK_EN         0u

#define OS_FLAG_EN                 1u
#define OS_MAX_FLAGS               8u
#define OS_FLAG_ACCEPT_EN          1u
#define OS_FLAG_QUERY_EN           1u
#define OS_FLAG_DEL_EN             1u
/* Flags on CLR not used by wrapper but must be defined for header completeness. */
#define OS_FLAG_WAIT_CLR_EN        0u
#define OS_FLAG_NAME_EN            0u

/* Select flag storage width used by ucos_ii.h typedef. */
#define OS_FLAGS_NBITS             32u

#define OS_SEM_EN                  1u
#define OS_SEM_QUERY_EN            1u
#define OS_SEM_SET_EN              1u
#define OS_SEM_ACCEPT_EN           1u
#define OS_SEM_DEL_EN              1u
#define OS_SEM_PEND_ABORT_EN        0u

#define OS_MUTEX_EN                1u
#define OS_MUTEX_ACCEPT_EN         1u
#define OS_MUTEX_DEL_EN            1u
#define OS_MUTEX_QUERY_EN          0u

#define OS_Q_EN                    1u
#define OS_MAX_QS                  8u
#define OS_Q_QUERY_EN              1u
#define OS_Q_FLUSH_EN              1u
#define OS_Q_DEL_EN                1u
#define OS_Q_ACCEPT_EN             1u
#define OS_Q_POST_EN               1u
#define OS_Q_PEND_ABORT_EN         0u
#define OS_Q_POST_FRONT_EN         0u
#define OS_Q_POST_OPT_EN           0u

#define OS_MBOX_EN                 0u
/* Mailbox options must be defined even when disabled. */
#define OS_MBOX_ACCEPT_EN          0u
#define OS_MBOX_DEL_EN             0u
#define OS_MBOX_PEND_ABORT_EN      0u
#define OS_MBOX_POST_EN            0u
#define OS_MBOX_POST_OPT_EN        0u
#define OS_MBOX_QUERY_EN           0u

#define OS_MEM_EN                  1u
#define OS_MAX_MEM_PART            4u
#define OS_MEM_NAME_EN             0u
#define OS_MEM_QUERY_EN            0u

#define OS_TMR_EN                  1u
#define OS_TMR_CFG_NAME_EN         0u
#define OS_TMR_CFG_TICKS_PER_SEC   100u

#define OS_SCHED_LOCK_EN           1u

#define OS_APP_HOOKS_EN            1u
#define OS_CPU_HOOKS_EN            0u

/* Misc options referenced by ucos_ii.h */
#define OS_TASK_STAT_EN            0u
#define OS_TASK_STAT_STK_SIZE      64u
#define OS_TASK_STAT_STK_CHK_EN    0u
#define OS_TICK_STEP_EN            0u

#define OS_LOWEST_PRIO             63u

/* Event control blocks. */
#define OS_MAX_EVENTS              16u
#define OS_EVENT_NAME_EN           0u

/* Time management. */
#define OS_TICKS_PER_SEC           1000u
#define OS_TIME_DLY_HMSM_EN        0u
#define OS_TIME_DLY_RESUME_EN      0u
#define OS_TIME_GET_SET_EN         1u
#define OS_TIME_TICK_HOOK_EN       0u

/* Misc global options required by headers. */
#define OS_ARG_CHK_EN              0u
#define OS_DEBUG_EN                0u
#define OS_EVENT_MULTI_EN          0u

/* Stack growth: 1 means down. */
#define OS_STK_GROWTH              1u

#endif /* OS_CFG_H */
