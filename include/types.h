#ifndef TYPES_H
# define TYPES_H

/* Fixed-width integer types for i386 (ILP32: int/long/pointer = 32 bit). */
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long long uint64_t;
typedef signed long long   int64_t;

typedef uint32_t           size_t;
typedef int32_t            ssize_t;
typedef uint32_t           uintptr_t;

typedef uint8_t            bool;
# define true  1
# define false 0

# define NULL ((void *)0)

/* Varargs via compiler builtins (no host headers in a freestanding kernel). */
typedef __builtin_va_list va_list;
# define va_start(ap, last) __builtin_va_start(ap, last)
# define va_arg(ap, type)   __builtin_va_arg(ap, type)
# define va_end(ap)         __builtin_va_end(ap)

#endif
