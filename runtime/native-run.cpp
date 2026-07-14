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
#include "recorder.h"
#include "section.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif

RECORDER(native_run, 16, "Native JIT execution");


static void dump_jit_code(void *mem, size_t sz, backend &be, cstring path)
// ----------------------------------------------------------------------------
//   Write JIT code to a file and log llvm-objdump disassembly via recorder
// ----------------------------------------------------------------------------
{
    if (!path || !*path)
        return;

    FILE *out = fopen(path, "wb");
    if (!out)
    {
        record(native_run, "JIT dump: cannot open %s", path);
        return;
    }
    fwrite(mem, 1, sz, out);
    fclose(out);
    record(native_run, "JIT dump %zu bytes -> %s", sz, path);

    for (auto const &entry : be.bytecode_positions())
        record(native_run, "  bc %p -> native %zu",
               (void *) entry.first, entry.second);

#if defined(__aarch64__)
    const char *arch = "aarch64";
#elif defined(__x86_64__) || defined(__amd64__)
    const char *arch = "x86_64";
#elif defined(__arm__)
    const char *arch = "arm";
#elif defined(__riscv)
    const char *arch = "riscv64";
#else
    const char *arch = nullptr;
#endif

    if (!arch)
        return;

    char cmd[640];
    char asm_path[256];
    char obj_path[256];
    snprintf(asm_path, sizeof(asm_path), "%s.s", path);
    snprintf(obj_path, sizeof(obj_path), "%s.o", path);
    snprintf(cmd, sizeof(cmd),
             "sh -c 'printf \".text\\n.globl _jit\\n_jit:\\n\" >\"%s\" && "
             "od -An -tx1 -v \"%s\" | awk \"{for (i=1;i<=NF;i++) "
             "printf \\\" .byte 0x%%s\\\\n\\\", \\$i}\" >>\"%s\" && "
             "llvm-mc -filetype=obj -arch=%s \"%s\" -o \"%s\" && "
             "llvm-objdump -d --no-show-raw-insn \"%s\"' 2>&1",
             asm_path, path, asm_path, arch, asm_path, obj_path, obj_path);
    record(native_run, "JIT disasm: %s", cmd);
    FILE *pipe = popen(cmd, "r");
    if (!pipe)
        return;
    char line[256];
    while (fgets(line, sizeof(line), pipe))
    {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n')
            line[n - 1] = '\0';
        record(native_run, "  %s", line);
    }
    pclose(pipe);
}


static void *map_executable(backend &be, byte_p data, size_t sz)
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
#if defined(__APPLE__)
    pthread_jit_write_protect_np(0);
#endif
    memcpy(mem, data, sz);
    be.relocate(mem);
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(mem, sz);
#elif defined(__linux__) && (defined(__aarch64__) || defined(__riscv))
    __builtin___clear_cache((char *) mem, (char *) mem + sz);
#endif
    if (mprotect(mem, alloc, PROT_READ | PROT_EXEC) != 0)
    {
        munmap(mem, alloc);
        return nullptr;
    }
    return mem;
#else
    (void) be;
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


#if defined(__i386__) || defined(__i386)
static void invoke_i386(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated i386 code with edi = rt.frame, esi = rt.flags
// ----------------------------------------------------------------------------
{
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn) code;
    uintptr_t framep = uintptr_t(rt.frame);
    uint32_t flags = uint32_t(rt.flags);
    asm volatile(
        "movl %0, %%edi\n\t"
        "movl %1, %%esi\n\t"
        "call *%2"
        :
        : "r"(framep), "r"(flags), "r"(entry)
        : "eax", "ecx", "edx", "edi", "esi", "memory", "cc");
}
#endif


#if defined(__aarch64__)
static void invoke_arm64(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated AArch64 code; x19 = rt.frame, x22 = rt.flags
// ----------------------------------------------------------------------------
{
    stack_p frame = rt.frame;
    uint64_t flags = rt.flags;
    asm volatile(
        "mov x19, %0\n\t"
        "mov x22, %1\n\t"
        "mov x16, %2\n\t"
        "blr x16"
        :
        : "r"(frame), "r"(flags), "r"(code)
        : "x16", "x19", "x22", "x30", "memory", "cc");
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


#if defined(__riscv)
static void invoke_riscv64(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated RISC-V code; s0 = rt.frame, s1 = rt.flags.
//   Save ra on the C stack so JIT may use ret after call_abs saves.
// ----------------------------------------------------------------------------
{
    stack_p frame = rt.frame;
    uint64_t flags = rt.flags;
    asm volatile(
        "mv s0, %0\n\t"
        "mv s1, %1\n\t"
        "addi sp, sp, -16\n\t"
        "sd ra, 0(sp)\n\t"
        "mv t0, %2\n\t"
        "jalr ra, t0, 0\n\t"
        "ld ra, 0(sp)\n\t"
        "addi sp, sp, 16"
        :
        : "r"(frame), "r"(flags), "r"(code)
        : "s0", "s1", "t0", "ra", "memory", "cc");
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

    void *mem = map_executable(be, code.data, sz);
    if (!mem)
        return false;

    rt.jit_code = mem;
    rt.jit_pc_off = be.bytecode_positions();

    if (cstring dump = getenv("XLR8_DUMP_JIT"))
        dump_jit_code(mem, sz, be, dump);

#if defined(__x86_64__) || defined(__amd64__)
    invoke_x86_64(rt, mem);
#elif defined(__i386__) || defined(__i386)
    invoke_i386(rt, mem);
#elif defined(__aarch64__)
    invoke_arm64(rt, mem);
#elif defined(__arm__)
    invoke_arm(rt, mem);
#elif defined(__riscv)
    invoke_riscv64(rt, mem);
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
