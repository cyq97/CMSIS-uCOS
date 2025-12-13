#ifndef OS_CPU_H
#define OS_CPU_H

/* Minimal CPU/port stubs for syntax-only compile checks. */

typedef unsigned int CPU_SR;

#define CPU_SR_ALLOC()      CPU_SR cpu_sr = 0u
#define CPU_CRITICAL_ENTER() do { (void)cpu_sr; } while (0)
#define CPU_CRITICAL_EXIT()  do { (void)cpu_sr; } while (0)

#endif /* OS_CPU_H */
