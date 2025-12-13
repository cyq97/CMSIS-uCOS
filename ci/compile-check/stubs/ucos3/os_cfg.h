#ifndef OS_CFG_H
#define OS_CFG_H

/* Minimal configuration for syntax-only compile checks. */

/* uC/LIB compatibility */
#ifndef DEF_ENABLED
#define DEF_ENABLED  1u
#endif
#ifndef DEF_DISABLED
#define DEF_DISABLED 0u
#endif

/* Core options required by CMSIS uCOS3 wrapper checks */
#define OS_CFG_TASK_DEL_EN        DEF_ENABLED
#define OS_CFG_TASK_SUSPEND_EN    DEF_ENABLED
#define OS_CFG_MUTEX_EN           DEF_ENABLED
#define OS_CFG_SEM_EN             DEF_ENABLED
#define OS_CFG_Q_EN               DEF_ENABLED
#define OS_CFG_FLAG_EN            DEF_ENABLED
#define OS_CFG_TMR_EN             DEF_ENABLED

/* Required by wrapper priority mapping */
#define OS_CFG_PRIO_MAX           64u

/* Tick rate used by wrapper */
#define OS_CFG_TICK_RATE_HZ       1000u

/* Enable APIs referenced by uC/OS-III headers */
#define OS_CFG_TASK_CHANGE_PRIO_EN        1u
#define OS_CFG_Q_DEL_EN                  1u
#define OS_CFG_Q_FLUSH_EN                1u
#define OS_CFG_SEM_DEL_EN                1u
#define OS_CFG_SEM_SET_EN                1u
#define OS_CFG_MUTEX_DEL_EN              1u
#define OS_CFG_FLAG_DEL_EN               1u
#define OS_CFG_TMR_DEL_EN                1u

/* Explicitly define options that uC/OS-III headers require to exist. */
#define OS_CFG_APP_HOOKS_EN             0u
#define OS_CFG_SCHED_LOCK_TIME_MEAS_EN  0u
#define OS_CFG_SCHED_ROUND_ROBIN_EN     0u
#define OS_CFG_STK_SIZE_MIN             64u

#define OS_CFG_STAT_TASK_EN              0u
#define OS_CFG_STAT_TASK_STK_CHK_EN      0u
#define OS_CFG_TASK_Q_EN                 0u
#define OS_CFG_TASK_Q_PEND_ABORT_EN      0u
#define OS_CFG_TASK_PROFILE_EN           0u
#define OS_CFG_TASK_REG_TBL_SIZE         0u
#define OS_CFG_TASK_SEM_PEND_ABORT_EN    0u
#define OS_CFG_TIME_DLY_HMSM_EN          0u
#define OS_CFG_TIME_DLY_RESUME_EN        0u
#define OS_CFG_TRACE_EN                  0u
#define OS_CFG_TRACE_API_ENTER_EN        0u
#define OS_CFG_TRACE_API_EXIT_EN         0u

#define OS_CFG_FLAG_MODE_CLR_EN          0u
#define OS_CFG_FLAG_PEND_ABORT_EN        0u

#define OS_CFG_MEM_EN                    0u

#define OS_CFG_MUTEX_PEND_ABORT_EN       0u
#define OS_CFG_Q_PEND_ABORT_EN           0u
#define OS_CFG_SEM_PEND_ABORT_EN         0u

/* Tick is required when timers are enabled. */
#define OS_CFG_TICK_EN                   1u
#define OS_CFG_DYN_TICK_EN               0u

/* Disable optional subsystems to keep stubs minimal */
#define OS_CFG_TS_EN                      0u
#define OS_CFG_DBG_EN                     0u
#define OS_CFG_CALLED_FROM_ISR_CHK_EN     0u
#define OS_CFG_INVALID_OS_CALLS_CHK_EN    0u
#define OS_CFG_ARG_CHK_EN                 0u
#define OS_CFG_OBJ_TYPE_CHK_EN            0u
#define OS_CFG_OBJ_CREATED_CHK_EN         0u

#endif /* OS_CFG_H */
