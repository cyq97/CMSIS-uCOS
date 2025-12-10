| CMSIS-RTOS2 特性 | 支持状态 | 说明 |
| --- | --- | --- |
| 内核初始化/启动/时钟 | ✅ | `osKernel*` 映射到 `OSInit/OSStart/OSTimeGet`、`OSSched{Lock,Unlock}` 等接口 |
| 线程创建/调度/优先级 | ✅ | 线程使用静态 `OS_TCB` + 栈；CMSIS 优先级压缩映射到 uC/OS-III 的 `OS_CFG_PRIO_MAX` 范围 |
| 线程挂起/恢复/锁 | ✅ | `osThreadYield/Delay/DelayUntil/Suspend/Resume` 基于 `OSTimeDly/OSTask*`；`osKernelLock/Unlock` 使用 `OSSched{Lock,Unlock}` |
| 线程 Flags API | ❌ | uC/OS-III 无线程级旗标机制，`osThreadFlags*` 返回 `osFlagsErrorUnsupported` |
| 事件 Flags 对象 | ✅ | 包装 `OSFlagCreate/Pend/Post/Del`，支持 WaitAll/WaitAny + 可选 NoClear |
| Mutex | ✅ | 基于 `OSMutex*`，仅支持非递归互斥；`osMutexRecursive` attr 将返回 `NULL` |
| Semaphore | ✅ | 使用 `OSSem*` 实现计数信号量，支持阻塞/非阻塞模式 |
| 定时器 | ✅ | 封装 `OSTmr*`，`osTimerStart` 通过 `OSTmrSet` 更新周期并启动 |
| 内存池 | ❌ | CMSIS `osMemoryPool*` 与 uC/OS-III `OSMem*` 语义差异较大，暂未封装 |
| 消息队列 | ✅* | 使用 `OS_Q` + 内部 `OS_SEM` 限制容量，仅支持指针消息 (`msg_size == sizeof(void*)`) |
| Kernel Protection / Zone / Watchdog | ❌ | uC/OS-III 无对应安全/监控 API |
| 线程本地存储 / 扩展 | ❌ | 内核未提供 CMSIS 期望的 TLS 能力 |

其他限制：

- 所有 CMSIS 对象（线程、互斥量、信号量、事件旗标、定时器、消息队列）都必须在 `osXxxAttr_t` 中提供静态控制块；封装层不会动态申请内存。
- 消息队列仅传递指针；`timeout == 0` 时，所有同步原语遵循 CMSIS 立即返回语义，对应 `OS_OPT_PEND_NON_BLOCKING`。
- 定时器 `ticks` 参数需大于 0；重复调用 `osTimerStart` 会自动更新 `OSTmr` 的延时/周期配置。