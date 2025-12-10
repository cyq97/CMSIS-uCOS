# uC/OS-II CMSIS-RTOS2 兼容层移植指南

本兼容层的目标是让使用 `cmsis_os2.h` 的应用可以在 uC/OS-II 上运行。由于 uC/OS-II 本身是静态内核，实现遵循“只套壳、不新增内核特性”的原则：所有 CMSIS 对象都需要用户在属性 (attr) 中提供控制块和必要的缓冲区，兼容层不会动态分配内存；uC/OS-II 缺少的功能 (如线程 Flags、Zone/Safety 等) 直接报告 `osErrorUnsupported`。

## 1. 依赖与配置

1. **包含头文件**：
   - 将 `CMSIS/RTOS2/Include` 添加到 include path；
   - 由于需要知道控制块大小，应用应同时包含 `CMSIS/RTOS2/uCOS2/Include/ucos2_os2.h` 以获得 `os_ucos2_*` 类型定义。
2. **使能 uC/OS-II 选项**：在 `os_cfg.h` 中须启用以下宏（模板文件 `libs/uC-OS2/Cfg/Template/os_cfg.h` 已全部置 1，可参考）：
   - 线程管理：`OS_TASK_CREATE_EN`、`OS_TASK_CREATE_EXT_EN`、`OS_TASK_NAME_EN`、`OS_TASK_DEL_EN`、`OS_TASK_SUSPEND_EN`、`OS_TASK_RESUME_EN`、`OS_TASK_CHANGE_PRIO_EN`。
   - 锁/延迟：`OS_SCHED_LOCK_EN`、`OS_TIME_DLY_RESUME_EN`。
   - 互斥量：`OS_MUTEX_EN`、`OS_MUTEX_ACCEPT_EN`、`OS_MUTEX_DEL_EN`。
   - 信号量：`OS_SEM_EN`、`OS_SEM_ACCEPT_EN`、`OS_SEM_QUERY_EN`、`OS_SEM_SET_EN`。
   - 队列：`OS_Q_EN`、`OS_Q_ACCEPT_EN`、`OS_Q_QUERY_EN`、`OS_Q_FLUSH_EN`、`OS_Q_DEL_EN`。
   - 事件旗标：`OS_FLAG_EN`、`OS_FLAG_ACCEPT_EN`、`OS_FLAG_QUERY_EN`。
   - 软件定时器：`OS_TMR_EN` 及相关参数。
   - 配置 `OS_LOWEST_PRIO` ≥  (50 + guard)，满足 `UCOS2_PRIORITY_LOWEST_AVAILABLE` 宏的断言。

若编译阶段报 `#error "Enable ..."`，请对照上述列表检查项目配置。

## 2. 静态对象分配示例

CMSIS attr 均需要提供 `cb_mem` 和其它缓冲：

| 对象 | attr 字段 | 说明 |
| --- | --- | --- |
| 线程 (`osThreadAttr_t`) | `cb_mem = os_ucos2_thread_t[]`<br>`stack_mem = uint8_t[]` | 栈大小建议 ≥ 256 bytes；控制块大小使用 `sizeof(os_ucos2_thread_t)` |
| 互斥量 (`osMutexAttr_t`) | `cb_mem = os_ucos2_mutex_t[]` | 仅支持非递归互斥；设置 `attr_bits` 包含 `osMutexPrioInherit` 不会生效 |
| 信号量 (`osSemaphoreAttr_t`) | `cb_mem = os_ucos2_semaphore_t[]` | `max_count` ≥ `initial_count` |
| 定时器 (`osTimerAttr_t`) | `cb_mem = os_ucos2_timer_t[]` | 每次 `osTimerStart` 会创建一个 uC/OS-II 软件定时器实例 |
| 事件旗标 (`osEventFlagsAttr_t`) | `cb_mem = os_ucos2_event_flags_t[]` | 仅支持等待“置位”动作 (WaitAll/WaitAny + NoClear) |
| 消息队列 (`osMessageQueueAttr_t`) | `cb_mem = os_ucos2_message_queue_t[]`<br>`mq_mem = void * storage[]` | 只允许指针消息 (即 `msg_size == sizeof(void*)`) |

## 3. 使用约束

- **0 超时语义**：Mutex、Semaphore、Message Queue、Event Flags 均支持 `timeout == 0` 的立即返回（内部使用 `Accept` 系列 API）。
- **消息队列**：
  - `msg_size` 必须等于指针宽度；`osMessageQueuePut/Get` 实际上传递的是 `void*`。
  - 用于 `mq_mem` 的缓冲需要能容纳 `msg_count` 个指针，即 `msg_count * sizeof(void*)` bytes。
- **定时器**：`ticks` 参数必须 > 0；重复 `osTimerStart` 会先删除旧实例再启动新实例。
- **线程 Flags API**：uC/OS-II 无对应概念，所有 `osThreadFlags*` 函数都会返回 `osFlagsErrorUnsupported`（已在 `SUPPORT.md` 说明）。
- **内存池 (`osMemoryPool*`)**：因与 uC/OS-II 的内存管理差异较大，暂未提供封装。
- **ISR 调用**：
  - 查询类 API（`osKernelGetInfo/GetState/GetTick*`、`osThreadGetId/GetName`、`osXxxGetName`）以及 `osSemaphoreRelease/osEventFlagsSet/Clear` 可在中断中使用。
  - `osSemaphoreAcquire` 与 `osMessageQueuePut/Get` 仅在 `timeout == 0` 的非阻塞模式下可在 ISR 调用；若资源不可用返回 `osErrorResource`。
  - 创建/删除任意 CMSIS 对象、`osTimer*`、`osMutex*`、`osEventFlagsWait` 等依赖调度的 API 在 ISR 中会返回 `osErrorISR`。

## 4. 初始化流程

1. 正常调用 `OSInit()` 之前不要创建 uC/OS-II 对象；使用 CMSIS API 时依次调用：
   ```c
   osKernelInitialize();
   // 创建线程/同步原语/定时器/消息队列
   osKernelStart();
   ```
2. CMSIS 示例(位于 `CMSIS/RTOS2/uCOS2/examples/basic/main.c`) 展示了基本用法：
   - 两个线程互相通过消息队列和信号量同步；
   - 互斥量保护共享计数；
   - 软件定时器周期性触发事件旗标；
   - 所有对象均使用静态属性配置。

## 5. 构建 & 集成

- 将 `CMSIS/RTOS2/Include` 与 `CMSIS/RTOS2/uCOS2/Include` 加入编译器搜索路径。
- 将 `CMSIS/RTOS2/uCOS2/Source/cmsis_os2_ucos2.c` 加入工程编译单元。
- 链接 uC/OS-II 源码及其端口文件（`Source/*.c` + 对应 `Ports/<arch>/*`）。
- 确保硬件定时器等 BSP 初始化完成后再调用 `osKernelStart()`。

## 6. 支持矩阵

完整功能支持表请查看 `CMSIS/RTOS2/uCOS2/SUPPORT.md`。若需扩展其它 CMSIS API，请确保 uC/OS-II 内核具备对应能力，再按同样方式包上一层。