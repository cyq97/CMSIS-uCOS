#ifndef CPU_CORE_H
#define CPU_CORE_H

#include <stdint.h>

/* uC/CPU core version required by uC/OS-III headers. */
#ifndef CPU_CORE_VERSION
#define CPU_CORE_VERSION 13103u
#endif

typedef uint8_t   CPU_BOOLEAN;
typedef char      CPU_CHAR;
typedef uintptr_t CPU_ADDR;

typedef uint8_t   CPU_INT08U;
typedef int8_t    CPU_INT08S;
typedef uint16_t  CPU_INT16U;
typedef int16_t   CPU_INT16S;
typedef uint32_t  CPU_INT32U;
typedef int32_t   CPU_INT32S;
typedef uint64_t  CPU_INT64U;
typedef int64_t   CPU_INT64S;

typedef uint32_t  CPU_DATA;

#ifndef CPU_CFG_DATA_SIZE
#define CPU_CFG_DATA_SIZE 4u
#endif

typedef uint32_t CPU_TS;

typedef uint32_t CPU_STK;
typedef uint32_t CPU_STK_SIZE;

#ifndef DEF_TRUE
#define DEF_TRUE  1u
#endif
#ifndef DEF_FALSE
#define DEF_FALSE 0u
#endif

#endif /* CPU_CORE_H */
