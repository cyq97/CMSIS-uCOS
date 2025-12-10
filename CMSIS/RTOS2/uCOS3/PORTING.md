# uC/OS-III CMSIS-RTOS2 兼容层移植指南

本兼容层用于让基于 `cmsis_os2.h` 的应用运行在 uC/OS-III 上。由于 uC/OS-III 使用静态控制块和编译期裁剪机制，封装层遵循“只套壳、不新增内核特性”的原则：所有 CMSIS 对象都需要用户在属性 (`attr`) 中提供控制块和必要的缓冲区；内核缺失的能力（例如线程旗标、TrustZone）直接返回 `osErrorUnsupported`。

## 1. 依赖与配置

1. **包含头文件**：
   - 将 `CMSIS/RTOS2/Include` 与 `CMSIS/RTOS2/uCOS3/Include` 加入编译器 include path；
   - 应用可包含 `ucos3_os2.h` 来获知控制块大小（`os_ucos3_*` 类型）。
2. **uC/OS-III 配置 (`os_cfg.h`)**：需要使能以下选项（`DEF_ENABLED`）：
   - 任务管理：`OS_CFG_TASK_DEL_EN`、`OS_CFG_TASK_SUSPEND_EN`、`OS_CFG_SCHED_LOCK_TIME_MEAS_EN`（可选，仅用于 `osKernelLock`）。
   - 同步原语：`OS_CFG_MUTEX_EN`、`OS_CFG_SEM_EN`、`OS_CFG_Q_EN`、`OS_CFG_FLAG_EN`。
   - 软件定时器：`OS_CFG_TMR_EN`，并确保计时任务已在 BSP 中启动。
   - 优先级：`OS_CFG_PRIO_MAX` 需 ≥ `UCOS3_PRIORITY_LEVELS + UCOS3_PRIORITY_GUARD + 1`（宏在 `ucos3_os2.h` 中检查）。
3. **其他建议**：保持 `OSTmrTask*`、`OSStatTask*` 等任务使用保留优先级，不要和 CMSIS 线程映射区冲突。

若编译阶段触发 `#error "Enable ..."`，请对照上述列表检查配置。

## 2. 静态对象分配示例

| CMSIS 对象 | attr 字段 | 说明 |
| --- | --- | --- |
| 线程 (`osThreadAttr_t`) | `cb_mem = os_ucos3_thread_t[]`<br>`stack_mem = CPU_STK[]` | 栈大小建议 ≥ 256 bytes；Joinable 线程会自动创建内部 `OS_SEM` |
| 互斥量 (`osMutexAttr_t`) | `cb_mem = os_ucos3_mutex_t[]` | 仅支持非递归互斥；`osMutexPrioInherit` 由 uC/OS-III 原生实现 |
| 信号量 (`osSemaphoreAttr_t`) | `cb_mem = os_ucos3_semaphore_t[]` | `max_count` ≥ `initial_count` |
| 事件旗标 (`osEventFlagsAttr_t`) | `cb_mem = os_ucos3_event_flags_t[]` | 等待语义为 WaitAll/WaitAny，支持可选 NoClear |
| 定时器 (`osTimerAttr_t`) | `cb_mem = os_ucos3_timer_t[]` | `ticks > 0`；`osTimerStart` 会调用 `OSTmrSet` 更新周期 |
| 消息队列 (`osMessageQueueAttr_t`) | `cb_mem = os_ucos3_message_queue_t[]` | 只接受指针消息 (`msg_size == sizeof(void*)`)，内部自带 `OS_SEM` 控制容量，`mq_mem` 可设为 `NULL` |

## 3. 使用约束

- **零超时语义**：Mutex/Semaphore/Message Queue/Event Flags 均通过 `OS_OPT_PEND_NON_BLOCKING` 支持 `timeout == 0` 的立即返回。
- **消息队列**：
  - 仅传递指针；`msg_size` 必须等于指针宽度；
  - 由于 uC/OS-III 的 `OS_Q` 不支持阻塞式 Post，封装层借助内部 `OS_SEM` 在 Put 路径上实现阻塞/无阻塞语义。
- **Joinable 线程**：`attr_bits` 含 `osThreadJoinable` 时会创建内部 `OS_SEM`；线程退出后需要调用 `osThreadJoin` 以释放控制块上的同步资源。
- **线程 Flags / 内存池**：尚未封装，相关 API 返回 `osFlagsErrorUnsupported` 或 `NULL`。
- **Tick 频率**：`osKernelGetTickFreq()`/`osKernelGetSysTimerFreq()` 返回 `OS_CFG_TICK_RATE_HZ`，若 BSP 修改系统节拍需同步更新配置。
- **ISR 调用**：
  - 查询类 API 与 `osSemaphoreRelease/osEventFlagsSet/Clear` 可在 ISR 中调用；
  - `osSemaphoreAcquire`、`osMessageQueuePut/Get` 仅在 `timeout == 0` 时支持 ISR 调用；资源不足返回 `osErrorResource`；
  - 对象创建/删除、`osTimer*`、`osMutex*`、`osEventFlagsWait` 等带调度行为的 API 在 ISR 中将返回 `osErrorISR`。

## 4. 初始化流程

1. BSP 完成 CPU、滴答定时器等底层初始化；
2. 调用者按 CMSIS 流程依次执行：
   ```c
   osKernelInitialize();
   // 创建线程/互斥量/信号量/事件旗标/定时器/消息队列
   osKernelStart();
   ```
3. `osKernelStart()` 内部直接调用 `OSStart()`，一旦调度器运行便不会返回。

## 5. 构建与集成

- 编译：将 `CMSIS/RTOS2/Include` 与 `CMSIS/RTOS2/uCOS3/Include` 加入 include path；把 `CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c` 加入工程编译单元。
- 链接：确保 uC/OS-III 核心源码、端口文件、BSP 支持文件均在同一映像中；`OSCfg_TmrTaskStkBase` 等符号需在应用中定义。
- 示例：`CMSIS/RTOS2/uCOS3/examples/basic/main.c` 展示了典型的静态对象创建方法，可作为移植起点。

## 6. 支持矩阵

参见 `CMSIS/RTOS2/uCOS3/SUPPORT.md` 了解每类 CMSIS-RTOS2 功能的实现状态及限制。