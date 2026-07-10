#ifndef ARCH_H
#define ARCH_H
// ****************************************************************************
//  arch.h                                                         XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Supported target architectures for the code generation backend.
//
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2026 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of XLR8.
//
//   XLR8 is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   XLR8 is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "../runtime/types.h"


enum arch_t
{
    ARCH_I386,
    ARCH_X86_64,
    ARCH_ARM64,
    ARCH_RISCV64,
#if defined(__x86_64__) || defined(__amd64__)
    ARCH_NATIVE = ARCH_X86_64,
#elif defined(__aarch64__)
    ARCH_NATIVE = ARCH_ARM64,
#elif defined(__riscv) && __riscv_xlen == 64
    ARCH_NATIVE = ARCH_RISCV64,
#elif defined(__i386__) || defined(__i386)
    ARCH_NATIVE = ARCH_I386,
#else
    ARCH_NATIVE = ARCH_X86_64,
#endif
};


cstring arch_name(arch_t arch);

#endif // ARCH_H
