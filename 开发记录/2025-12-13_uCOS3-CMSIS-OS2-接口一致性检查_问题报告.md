# uCOS3 CMSIS-RTOS2 兼容层：uC/OS-III 接口一致性检查问题报告

- **日期**：2025-12-13T01:04:13+00:00
- **仓库分支**：`cursor/ucos3-interface-compatibility-check-6be2`
- **检查目标**：核对仓库内 uCOS3 的 CMSIS-RTOS2 兼容层所使用的 uC/OS-III API，是否与本仓库 `libs/uC-OS3` 子模块版本一致。

## 1. 被检查的代码/版本

- **兼容层实现**：`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c`
- **兼容层头文件**：`CMSIS/RTOS2/uCOS3/Include/ucos3_os2.h`
- **uC/OS-III 子模块**：`libs/uC-OS3`（`weston-embedded/uC-OS3`）
  - **子模块提交**：`93d6e79023f1031480c20f479ca7662f3f1c3c3a`
  - **子模块说明**：提交信息为 `Merge pull request #34 ... release-v3.08.02`（即 v3.08.02 系列接口）
  - **关键头文件**：`libs/uC-OS3/Source/os.h`

## 2. 兼容层使用到的 uC/OS-III 符号概览

从 `cmsis_os2_ucos3.c` 中抽取到的 uC/OS-III 相关符号（函数/全局变量/枚举/宏）共 **99** 个（去重后）。

## 3. 发现的问题（接口不一致）

结论：**当前兼容层并非严格基于 `libs/uC-OS3`（v3.08.02）接口实现**，存在多处符号缺失/签名不一致，可能导致编译失败或行为不一致。

### 3.1 子模块中不存在的符号（会直接编译失败）

1) **`OSTaskYield`**
- **兼容层使用位置**：`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:662`
- **子模块现状**：`libs/uC-OS3/Source/os.h` 中未声明 `OSTaskYield()`。
- **影响**：`osThreadYield()` 无法通过编译。

2) **`OSSemCntGet`**
- **兼容层使用位置**：
  - `CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:1102`（`osSemaphoreGetCount`）
  - `CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:1567`（用于消息队列空间计数）
- **子模块现状**：`os.h` 中未声明 `OSSemCntGet()`。
- **影响**：计数查询相关 API 无法通过编译。

3) **`OSQMsgQtyGet`**
- **兼容层使用位置**：`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:1556`
- **子模块现状**：`os.h` 中未声明 `OSQMsgQtyGet()`。
- **影响**：`osMessageQueueGetCount()` 无法通过编译。

4) **`OSFlagQuery`**
- **兼容层使用位置**：`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:1336`
- **子模块现状**：`os.h` 中未声明 `OSFlagQuery()`（仅有 `OSFlagCreate/Del/Pend/Post` 以及内部辅助 `OSFlagPendGetFlagsRdy` 等）。
- **影响**：`osEventFlagsGet()` 无法通过编译。

5) **`OS_ERR_FLAG_INVALID` / `OS_ERR_FLAG_INVALID_PEND_OPT`**
- **兼容层使用位置**：`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:88-90`（错误码映射）
- **子模块现状**：`os.h` 中对应命名为 `OS_ERR_FLAG_PEND_OPT` 等（不存在这两个枚举名）。
- **影响**：错误码分支无法通过编译（或需改为子模块真实的枚举值）。

6) **`OS_TMR_STATE` 类型**
- **兼容层使用位置**：`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:1248`
- **子模块现状**：`OSTmrStateGet()` 返回类型为 `OS_STATE`，并用 `OS_TMR_STATE_RUNNING` 等宏表示状态；`os.h` 未定义 `OS_TMR_STATE` 类型别名。
- **影响**：定时器状态变量声明可能编译失败（或类型不匹配）。

### 3.2 函数签名不一致（会直接编译失败）

1) **`OSTmrSet` 调用参数数量不匹配**
- **兼容层调用位置**：`CMSIS/RTOS2/uCOS3/Source/cmsis_os2_ucos3.c:1186`
- **兼容层调用方式**：向 `OSTmrSet()` 传入了 `opt`（`OS_OPT_TMR_PERIODIC/OS_OPT_TMR_ONE_SHOT`）等参数。
- **子模块真实原型**（`libs/uC-OS3/Source/os.h`）：
  - `void OSTmrSet(OS_TMR *p_tmr, OS_TICK dly, OS_TICK period, OS_TMR_CALLBACK_PTR p_callback, void *p_callback_arg, OS_ERR *p_err);`
  - **注意**：子模块版本的 `OSTmrSet()` **不带** `opt` 参数。
- **影响**：`osTimerStart()`/`osUcos3TimerConfigure()` 无法通过编译。

## 4. 初步建议（后续修复方向）

- **目标**：让 `CMSIS/RTOS2/uCOS3` 兼容层严格对齐 `libs/uC-OS3`（v3.08.02）接口。
- **建议改动点**（高层方向，不在本报告内直接改代码）：
  - 用子模块存在的调度/让出接口替换 `OSTaskYield()`（例如使用 `OSSchedRoundRobinYield()` 或通过其他等价机制实现 `osThreadYield()` 语义）。
  - 用结构字段/已有 API 替换 `OSSemCntGet()`、`OSQMsgQtyGet()`、`OSFlagQuery()`：
    - `OS_SEM`：可从 `Ctr` 字段读取计数（注意并发一致性/临界区策略）；
    - `OS_Q`：可从 `MsgQ.NbrEntries` 等字段读取数量（同上）；
    - `OS_FLAG_GRP`：可从 `Flags` 字段读取当前旗标（同上）。
  - 错误码映射：将 `OS_ERR_FLAG_INVALID*` 替换为子模块实际存在的 `OS_ERR_*` 枚举名（如 `OS_ERR_FLAG_PEND_OPT` 等）。
  - 定时器：按子模块 `OSTmrSet()` 原型调整封装逻辑（通过 `period` 是否为 0 来表达 one-shot vs periodic，并结合 `OSTmrCreate()` 时的 opt 选择）。

## 5. 附：本次核对中确认“存在且原型匹配”的核心接口（抽样）

以下接口在子模块 `os.h` 中存在，且与兼容层调用形式一致（示例）：
- `OSInit`, `OSStart`
- 任务：`OSTaskCreate`, `OSTaskDel`, `OSTaskSuspend`, `OSTaskResume`, `OSTaskChangePrio`
- 互斥量：`OSMutexCreate`, `OSMutexPend`, `OSMutexPost`, `OSMutexDel`
- 信号量：`OSSemCreate`, `OSSemPend`, `OSSemPost`, `OSSemDel`, `OSSemSet`
- 旗标：`OSFlagCreate`, `OSFlagPend`, `OSFlagPost`, `OSFlagDel`
- 队列：`OSQCreate`, `OSQPend`, `OSQPost`, `OSQFlush`, `OSQDel`
- 定时/时间：`OSTimeGet`, `OSTimeDly`, `OSTmrCreate`, `OSTmrStart`, `OSTmrStop`, `OSTmrDel`, `OSTmrStateGet`

---

**状态**：已输出问题报告；待后续决定是否按 `libs/uC-OS3 v3.08.02` 对兼容层进行适配修复。
