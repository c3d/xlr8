// ****************************************************************************
//  native-run.cpp                                                 XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Translate bytecode to native code and execute it on the host CPU.
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

#include "runtime.h"
#include "native-bridge.h"

#include "arch.h"
#include "backend.h"
#include "section.h"

#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif


static void *map_executable(byte_p data, size_t sz)
// ----------------------------------------------------------------------------
//   Copy generated code into an executable memory mapping
// ----------------------------------------------------------------------------
{
#if defined(__unix__) || defined(__APPLE__)
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return nullptr;
    size_t alloc = (sz + size_t(page) - 1) & ~(size_t(page) - 1);
    int flags = MAP_PRIVATE | MAP_ANON;
#if defined(__APPLE__)
    flags |= MAP_JIT;
#endif
    void *mem = mmap(nullptr, alloc, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mem == MAP_FAILED)
        return nullptr;
    memcpy(mem, data, sz);
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(mem, sz);
#endif
    if (mprotect(mem, alloc, PROT_READ | PROT_EXEC) != 0)
    {
        munmap(mem, alloc);
        return nullptr;
    }
    return mem;
#else
    (void) data;
    (void) sz;
    return nullptr;
#endif
}


static void unmap_executable(void *mem, size_t sz)
// ----------------------------------------------------------------------------
//   Release an executable memory mapping
// ----------------------------------------------------------------------------
{
#if defined(__unix__) || defined(__APPLE__)
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return;
    size_t alloc = (sz + size_t(page) - 1) & ~(size_t(page) - 1);
    munmap(mem, alloc);
#else
    (void) mem;
    (void) sz;
#endif
}


#if defined(__x86_64__) || defined(__amd64__)
static void invoke_x86_64(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated x86-64 code with frame and flags registers primed
// ----------------------------------------------------------------------------
{
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn) code;
    stack_p frame = rt.frame;
    uint64_t flags = rt.flags;
    asm volatile(
        "mov %1, %%r12\n\t"
        "mov %2, %%r15\n\t"
        "call *%0"
        :
        : "r"(entry), "r"(frame), "r"(flags)
        : "r12", "r15", "memory", "cc");
}
#endif


#if defined(__aarch64__)
static void invoke_arm64(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated AArch64 code; x19 = rt.frame, x22 = rt.flags
// ----------------------------------------------------------------------------
{
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn) code;
    stack_p frame = rt.frame;
    uint64_t flags = rt.flags;
    asm volatile(
        "mov x19, %0\n\t"
        "mov x22, %1"
        :
        : "r"(frame), "r"(flags)
        : "x19", "x22", "memory");
    entry();
}
#endif


#if defined(__arm__) && !defined(__aarch64__)
static void invoke_arm(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated 32-bit ARM code; r9 = rt.frame, r10 = rt.flags
// ----------------------------------------------------------------------------
{
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn) code;
    stack_p frame = rt.frame;
    uint64_t flags = rt.flags;
    asm volatile(
        "mov r9, %0\n\t"
        "mov r10, %1"
        :
        : "r"(frame), "r"(flags)
        : "r9", "r10", "memory");
    entry();
}
#endif


static bool native_run_impl(runtime &rt, backend &be, byte_p entry)
// ----------------------------------------------------------------------------
//   Translate bytecode and execute the generated native code
// ----------------------------------------------------------------------------
{
    xlr8_set_runtime(&rt);
    byte_p pc = entry;
    if (!be.translate(pc))
        return false;

    section &code = be.code();
    size_t sz = code.size();
    if (!sz)
        return true;

    void *mem = map_executable(code.data, sz);
    if (!mem)
        return false;

#if defined(__x86_64__) || defined(__amd64__)
    invoke_x86_64(rt, mem);
#elif defined(__aarch64__)
    invoke_arm64(rt, mem);
#elif defined(__arm__)
    invoke_arm(rt, mem);
#else
    unmap_executable(mem, sz);
    rt.error("Native execution is not supported on this CPU");
    return false;
#endif

    unmap_executable(mem, sz);
    return rt.error() == nullptr;
}


bool runtime::run_native(byte_p entry)
// ----------------------------------------------------------------------------
//   Compile entry bytecode to native code and run it on the host CPU
// ----------------------------------------------------------------------------
{
    if (!entry)
        entry = this->entry;
    backend *be = backend_new(ARCH_NATIVE);
    if (!be)
    {
        error("No native backend for this CPU");
        return false;
    }
    bool ok = native_run_impl(*this, *be, entry);
    delete be;
    return ok;
}
