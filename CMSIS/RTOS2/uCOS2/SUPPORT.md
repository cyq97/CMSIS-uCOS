| CMSIS-RTOS2 特性 | 支持状态 | 说明 |
| --- | --- | --- |
| 内核初始化/启动/时钟 | ✅ | 直接映射到 `OSInit/OSStart/OSTimeGet` 等 API |
| 线程创建/调度/优先级 | ✅ | 需提供静态控制块与栈；优先级压缩映射至 uC/OS-II 56 个逻辑级别 |
| 线程挂起/恢复/锁 | ✅ | `osThreadYield/Delay/DelayUntil/Suspend/Resume` 均映射到 `OSTask*` |
| 线程 Flags API | ❌ | uC/OS-II 无线程级旗标功能，无法直接兼容 |
| 事件 Flags 对象 | ✅ | 基于 `OSFlag*` 实现 `osEventFlagsNew/Set/Clear/Wait/Delete`（线程旗标仍不支持） |
| Mutex | ✅ | 基于 `OSMutex*`，仅支持非递归互斥；`osMutexRecursive` attr 将返回 `NULL` |
| Semaphore | ✅ | 基于 `OSSem*`，支持计数信号量，全部静态创建 |
| 定时器 | ✅ | 使用 uC/OS-II 软件定时器；`osTimerStart` 每次会重新创建内核定时器以便调整周期 |
| 内存池 | ❌ | uC/OS-II 的内存分区与 CMSIS 语义差异较大，当前未适配 |
| 消息队列 | ✅* | 使用 uC/OS-II 队列（指针消息）；仅支持 `msg_size == sizeof(void*)`，超出返回 `NULL` |
| Kernel Protection / Zone / Watchdog | ❌ | 对应 CMSIS 高级安全接口在 uC/OS-II 中无等价功能 |
| 线程本地存储 / 扩展 | ❌ | uC/OS-II 缺少 CMSIS 所需 TLS 机制，暂未封装 |

其他限制：

- 所有 CMSIS 对象（线程、互斥量、信号量、定时器、消息队列）都必须在 `osXxxAttr_t` 中提供静态控制块及必要缓冲；兼容层不会动态申请内存。
- 消息队列只传递指针（`msg_size` 必须等于平台指针宽度）；`timeout == 0` 时所有同步原语（ mutex / semaphore / message queue ）都会立即返回以符合 CMSIS 语义。
- 定时器 `ticks` 参数需大于 0；若重复调用 `osTimerStart`，内部会先停止/删除旧定时器再按新周期重建。
