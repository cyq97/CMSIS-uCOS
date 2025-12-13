#ifndef OS_CPU_H
#define OS_CPU_H

#include <stdint.h>

typedef uint8_t   INT8U;
typedef int8_t    INT8S;
typedef uint16_t  INT16U;
typedef int16_t   INT16S;
typedef uint32_t  INT32U;
typedef int32_t   INT32S;

typedef uint32_t  OS_STK;

typedef uint8_t   BOOLEAN;

#define OS_ENTER_CRITICAL() do {} while (0)
#define OS_EXIT_CRITICAL()  do {} while (0)

#endif /* OS_CPU_H */
