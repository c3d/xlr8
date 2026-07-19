// ****************************************************************************
//  backend.cpp                                                    XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Base class for machine-specific code generation backends
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

#include "backend.h"
#include "arm.h"
#include "arm64.h"
#include "i386.h"
#include "riscv64.h"
#include "x86_64.h"

#include <cctype>
#include <cstring>


RECORDER(backend,          256, "Architecture-neutral code generation");
RECORDER(backend_error,     64, "Architecture-neutral code generation errors");
RECORDER(backend_warning,   64, "Architecture-neutral code generation warnings");


backend::backend(arch_t arch)
// ----------------------------------------------------------------------------
//   Initialize shared backend state
// ----------------------------------------------------------------------------
    : current_arch(arch),
      current_output(OUTPUT_MEMORY),
      nocode_wanted(false),
      verbose(false),
      text_section(),
      ind(),
      loc()
{
}


void backend::g(byte c)
// ----------------------------------------------------------------------------
//   Emit one byte into the current text section
// ----------------------------------------------------------------------------
{
    if (nocode_wanted)
        return;
    if (ind >= text_section.size())
        text_section.resize(ind + 1);
    text_section[ind] = c;
    ind++;
}


void backend::emit32le(uint32_t c)
// ----------------------------------------------------------------------------
//   Emit a 32-bit little-endian value
// ----------------------------------------------------------------------------
{
    record(backend, "emit32le(0x%llX)", c);
    g(byte(c));
    g(byte(c >> 8));
    g(byte(c >> 16));
    g(byte(c >> 24));
}


void backend::gsym(uint t)
// ----------------------------------------------------------------------------
//   Patch pending jumps to the current code position
// ----------------------------------------------------------------------------
{
    gsym_addr(t, uint(ind));
}


void backend::set_output(output_t output)
// ----------------------------------------------------------------------------
//   Select the output format (memory, object file, executable, ...)
// ----------------------------------------------------------------------------
{
    current_output = output;
}


bool backend::set_options(cstring options)
// ----------------------------------------------------------------------------
//   Parse backend options (TinyCC's tcc_set_options analogue)
// ----------------------------------------------------------------------------
{
    if (!options)
        return true;

    std::string opt = options;
    size_t pos = 0;
    while (pos < opt.size())
    {
        while (pos < opt.size() && std::isspace(opt[pos]))
            pos++;
        if (pos >= opt.size())
            break;

        size_t start = pos;
        while (pos < opt.size() && !std::isspace(opt[pos]))
            pos++;
        std::string token = opt.substr(start, pos - start);

        if (token == "-v" || token == "--verbose")
            verbose = true;
        else if (token == "-nostdlib")
            { /* reserved for future link options */ }
        else if (token == "-m32")
        {
            if (current_arch != ARCH_I386)
                return false;
        }
        else if (token == "-m64")
        {
            if (current_arch != ARCH_X86_64)
                return false;
        }
        else if (token == "-marm" || token == "-marm32")
        {
            if (current_arch != ARCH_ARM)
                return false;
        }
        else if (token == "-marm64" || token == "-maarch64")
        {
            if (current_arch != ARCH_ARM64)
                return false;
        }
        else if (token == "-mriscv64")
        {
            if (current_arch != ARCH_RISCV64)
                return false;
        }
        else if (token == "-r" || token == "-c")
            set_output(OUTPUT_OBJ);
        else if (token == "-shared")
            set_output(OUTPUT_DLL);
        else if (token.substr(0, 2) == "-O")
            { /* optimization level - reserved */ }
        else
            return false;
    }
    return true;
}


backend *backend_new(arch_t arch)
// ----------------------------------------------------------------------------
//   Create a code generator for the requested architecture
// ----------------------------------------------------------------------------
{
    switch (arch)
    {
    case ARCH_I386:     return i386_be::make();
    case ARCH_X86_64:   return x86_64::make();
    case ARCH_ARM:      return arm::make();
    case ARCH_ARM64:    return arm64::make();
    case ARCH_RISCV64:  return riscv64::make();
    }
    return x86_64::make();
}
