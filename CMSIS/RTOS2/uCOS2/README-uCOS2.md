# uC/OS-II CMSIS-RTOS2 兼容层概览

本目录中的 `cmsis_os2_ucos2.c` 为 CMSIS-RTOS2 API 在 uC/OS-II 上的适配层，遵循“只封装内核已有能力、不新增特性”的原则：

## 已实现的 CMSIS API

- **Kernel**：`osKernelInitialize/GetInfo/GetState/Start/Lock/Unlock/RestoreLock/GetTick*`。
- **Thread**：`osThreadNew/GetId/GetName/GetState/SetPriority/GetPriority/Yield/Delay/DelayUntil/Suspend/Resume/Detach/Join/Terminate/Exit`。
- **Mutex**：基于 `OSMutex*`，仅支持非递归互斥；`timeout == 0` 使用 `OSMutexAccept` 实现非阻塞。
- **Semaphore**：基于 `OSSem*`，支持计数信号量及立即返回模式 (`OSSemAccept`)。
- **Timer**：包装 uC/OS-II 软件定时器；`osTimerStart` 传入 ticks，内部创建/重建 `OSTmrCreate` 实例。
- **Event Flags**：封装 `OSFlagCreate/Accept/Pend/Post`；仅支持等待置位 (WaitAll/Any + NoClear)。线程 Flags API 仍返回 `osFlagsErrorUnsupported`。
- **Message Queue**：基于 uC/OS-II 队列 + 空闲信号量，只允许指针消息 (`msg_size == sizeof(void*)`)；`timeout == 0` 使用 `OSSemAccept/OSQAccept` 实现非阻塞。

## 未实现或限制的功能

- **线程 Flags** (`osThreadFlags*`)：uC/OS-II 无对应机制，直接返回 `osFlagsErrorUnsupported`。
- **内存池** (`osMemoryPool*`)：暂未封装。
- **高级安全/Zone/Watchdog**：CMSIS-RTOS2 中与 TrustZone、Watchdog 相关的 API 在 uC/OS-II 中无等价实现。

完整支持矩阵及限制详见 `CMSIS/RTOS2/uCOS2/SUPPORT.md`。

## 静态对象要求

所有 CMSIS 对象都需要通过 attr 传入静态控制块：

| 类型 | 控制块类型 | 说明 |
| --- | --- | --- |
| 线程 | `os_ucos2_thread_t` + 栈缓冲 | 栈大小建议 ≥ 256 bytes |
| 互斥量 | `os_ucos2_mutex_t` | 不支持 `osMutexRecursive` |
| 信号量 | `os_ucos2_semaphore_t` | `max_count` ≥ `initial_count` |
| 事件旗标 | `os_ucos2_event_flags_t` | 等待置位语义 |
| 定时器 | `os_ucos2_timer_t` | `ticks` > 0；周期/一次性均可 |
| 消息队列 | `os_ucos2_message_queue_t` + 指针数组 | `msg_size == sizeof(void*)` |

## 示例与移植指南

- `examples/basic/main.c`：演示如何静态创建线程、互斥量、信号量、事件旗标、定时器及指针消息队列，构建生产者-消费者模型。
- `PORTING.md`：列出所需配置宏、静态 attr 写法、集成步骤与注意事项。

后续若需扩展其它 CMSIS API，可在确认 uC/OS-II 支持后，参照当前模式进行封装。