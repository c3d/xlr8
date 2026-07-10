// ****************************************************************************
//  arch.cpp                                                        XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Architecture name helpers
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

#include "arch.h"


cstring arch_name(arch_t arch)
// ----------------------------------------------------------------------------
//   Return a human-readable name for an architecture
// ----------------------------------------------------------------------------
{
    switch (arch)
    {
    case ARCH_I386:     return "i386";
    case ARCH_X86_64:   return "x86_64";
    case ARCH_ARM:      return "arm";
    case ARCH_ARM64:    return "arm64";
    case ARCH_RISCV64:  return "riscv64";
    }
    return "unknown";
}
