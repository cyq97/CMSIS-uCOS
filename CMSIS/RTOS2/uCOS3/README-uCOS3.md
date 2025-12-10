# uC/OS-III CMSIS-RTOS2 兼容层概览

`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c` 提供了在 uC/OS-III 内核上运行 CMSIS-RTOS2 API 的静态封装。实现遵循“只复用已有内核特性、不新增动态机制”的原则，所有对象都依赖调用者在 `attr` 中提供控制块和必要的缓冲区。

## 已实现的 CMSIS API

- **内核**：`osKernelInitialize/GetInfo/GetState/Start/Lock/Unlock/RestoreLock/GetTick*` 对应 `OSInit/OSStart/OSSched{Lock,Unlock}` 等接口。
- **线程**：`osThreadNew/GetId/GetName/GetState/SetPriority/GetPriority/Yield/Delay/DelayUntil/Suspend/Resume/Detach/Join/Terminate/Exit` 通过 `OSTaskCreate/Del/Suspend/Resume/ChangePrio` 完成，支持 Joinable 语义（基于内部 `OS_SEM`）。
- **互斥量**：包装 `OSMutex*`，仅支持非递归互斥；`timeout == 0` 通过 `OS_OPT_PEND_NON_BLOCKING` 实现立即返回。
- **信号量**：基于 `OSSem*`，支持计数信号量、无限等待及零等待模式。
- **定时器**：封装 `OSTmr*`，每次 `osTimerStart` 通过 `OSTmrSet` 更新周期，支持一次性与周期性模式。
- **事件旗标**：映射到 `OSFlagCreate/Pend/Post/Del`，提供 WaitAll/WaitAny 与可选的 NoClear 语义；线程 Flags API 仍返回 `osFlagsErrorUnsupported`。
- **消息队列**：使用 `OS_Q` + 辅助 `OS_SEM` 限制容量，只允许指针消息 (`msg_size == sizeof(void*)`)，支持阻塞/非阻塞 Put/Get。

## 未实现或限制

- **线程 Flags (`osThreadFlags*`)**：uC/OS-III 不提供线程私有旗标，接口固定返回 `osFlagsErrorUnsupported`。
- **内存池 (`osMemoryPool*`)**：暂未封装，推荐改用 uC/OS-III 的 `OSMem*` 或其它自定义分配器。
- **TrustZone/Safety/Watchdog 等高级特性**：内核无对应功能。
- **对象动态分配**：兼容层不会调用 `malloc`，所有 CMSIS 对象都需要调用者提供静态控制块及（若需要）缓冲区。

完整支持矩阵及限制详见 `CMSIS/RTOS2/uCOS3/SUPPORT.md`。

## 静态对象要求

| CMSIS 类型 | 控制块/缓冲区 | 说明 |
| --- | --- | --- |
| 线程 | `os_ucos3_thread_t` + 栈缓冲 (`CPU_STK[]`) | 栈大小 ≥ 256 bytes 建议；Joinable 线程会额外创建内部 `OS_SEM` |
| 互斥量 | `os_ucos3_mutex_t` | 仅支持非递归互斥，优先级继承由内核负责 |
| 信号量 | `os_ucos3_semaphore_t` | `max_count` ≥ `initial_count` |
| 事件旗标 | `os_ucos3_event_flags_t` | 仅实现 WaitAll/WaitAny + 可选 NoClear |
| 定时器 | `os_ucos3_timer_t` | `ticks > 0`；周期/一次性均可 |
| 消息队列 | `os_ucos3_message_queue_t` (+ 内部 `OS_SEM`) | 只接受指针消息，容量由 `msg_count` 指定 |

## 中断上下文支持

- 查询类 API (`osKernelGetInfo/GetState/GetTick*`、`osThreadGetId/GetName`、`osXxxGetName`) 可直接在 ISR 中使用。
- `osSemaphoreRelease`、`osEventFlagsSet/Clear` 以及 `osMessageQueuePut/Get` 在 ISR 中支持 **零超时** 非阻塞调用；资源不可用时返回 `osErrorResource`。
- `osSemaphoreAcquire` 同样只允许在 ISR 内执行零超时尝试，`timeout > 0` 会立即返回 `osErrorParameter`。
- 所有创建/删除对象、`osTimer*`、`osMutex*`、`osEventFlagsWait` 等需要调度的 API 在 ISR 中会返回 `osErrorISR`。

## 示例与移植资料

- `examples/basic/main.c` 展示了如何在 uC/OS-III 中静态创建线程、互斥量、信号量、事件旗标、定时器与消息队列，构建简单的生产者/消费者场景。
- `PORTING.md` 详述所需的 `OS_CFG_*` 配置、attr 写法、集成步骤与注意事项。

若需扩展其它 CMSIS API，请先确认 uC/OS-III 内核具备等价能力，再按当前模式封装。