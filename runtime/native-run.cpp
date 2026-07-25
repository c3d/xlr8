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

#include "backend.h"
#include "recorder.h"
#include "section.h"
#include "native-bridge.h"

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

RECORDER(native_run,            16, "Native JIT execution");
RECORDER(native_run_error,      16, "Native JIT errors");

static backend *xlr8_backend = nullptr;


static void dump_jit_code(void *mem, size_t sz, backend &be, cstring path)
// ----------------------------------------------------------------------------
//   Write llvm-objdump disassembly of JIT code to path (instruction listing)
// ----------------------------------------------------------------------------
{
    if (!path || !*path)
        return;

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
    {
        record(native_run_error, "JIT disasm: unsupported host arch");
        return;
    }

    // Raw blob + assemble intermediates; the user path gets the listing.
    size_t plen = strlen(path);
    char  *bin_path = (char *) malloc(plen + 5);  // .bin
    char  *asm_path = (char *) malloc(plen + 9);  // .tmp.asm
    char  *obj_path = (char *) malloc(plen + 5);  // .tmp.o
    if (!bin_path || !asm_path || !obj_path)
    {
        free(bin_path);
        free(asm_path);
        free(obj_path);
        record(native_run_error, "JIT disasm OOM");
        return;
    }
    snprintf(bin_path, plen + 5, "%s.bin", path);
    snprintf(asm_path, plen + 9, "%s.tmp.asm", path);
    snprintf(obj_path, plen + 5, "%s.o", path);

    FILE *out = fopen(bin_path, "wb");
    if (!out)
    {
        record(native_run_error,
               "JIT dump cannot open %s: %s", bin_path, strerror(errno));
        free(bin_path);
        free(asm_path);
        free(obj_path);
        return;
    }
    fwrite(mem, 1, sz, out);
    fclose(out);
    record(native_run, "JIT dump %zu bytes -> %s", sz, bin_path);

    for (auto const &label : be.address_translations())
        record(native_run, "  bc %p -> native %zu",
               label.bytecode, label.hwcode);

    // Assemble .byte blob then objdump into the user-facing listing file.
    const char *cmd_fmt =
        "sh -c 'printf \".text\\n.globl _jit\\n_jit:\\n\" >\"%s\" && "
        "od -An -tx1 -v \"%s\" | "
        "awk \"{for (i=1;i<=NF;i++) printf \\\" .byte 0x%%s\\\\n\\\", \\$i}\" "
        ">>\"%s\" && "
        "llvm-mc -filetype=obj -arch=%s \"%s\" -o \"%s\" && "
        "llvm-objdump -d --no-show-raw-insn \"%s\" >\"%s\"' 2>&1";

    char *cmd     = nullptr;
    int   cmd_len = snprintf(nullptr, 0, cmd_fmt,
                             asm_path, bin_path, asm_path, arch,
                             asm_path, obj_path, obj_path, path);
    if (cmd_len < 0)
    {
        record(native_run_error,
               "JIT disasm command format error: %s",
               strerror(errno));
        free(bin_path);
        free(asm_path);
        free(obj_path);
        return;
    }
    cmd = (char *) malloc(size_t(cmd_len) + 1);
    if (!cmd)
    {
        record(native_run_error, "JIT disasm OOM allocating command");
        free(bin_path);
        free(asm_path);
        free(obj_path);
        return;
    }
    snprintf(cmd, size_t(cmd_len) + 1, cmd_fmt,
             asm_path, bin_path, asm_path, arch,
             asm_path, obj_path, obj_path, path);
    record(native_run, "JIT disasm:\n%s", cmd);
    FILE *pipe = popen(cmd, "r");
    free(cmd);
    if (pipe)
    {
        char line[256];
        while (fgets(line, sizeof(line), pipe))
        {
            size_t n = strlen(line);
            if (n && line[n - 1] == '\n')
                line[n - 1] = '\0';
            if (line[0])
                record(native_run_error, "  %s", line);
        }
        pclose(pipe);
    }

    // Echo listing to recorder for convenience
    FILE *listing = fopen(path, "r");
    if (listing)
    {
        record(native_run, "JIT listing %s:", path);
        char line[256];
        while (fgets(line, sizeof(line), listing))
        {
            size_t n = strlen(line);
            if (n && line[n - 1] == '\n')
                line[n - 1] = '\0';
            record(native_run, "  %s", line);
        }
        fclose(listing);
    }
    else
    {
        record(native_run_error,
               "JIT disasm produced no listing at %s", path);
    }

    unlink(asm_path);
    unlink(obj_path);
    unlink(bin_path);
    free(bin_path);
    free(asm_path);
    free(obj_path);
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


static inline stack_p initial_jit_fp(ularge_t slots[8 + 2])
// ----------------------------------------------------------------------------
//   Caller-provided JIT frame (all targets)
// ----------------------------------------------------------------------------
//   Backends load/store locals at [FP + local_disp(n)] with
//   local_disp(n) = ~(n) * sizeof(ularge_t) (local 0 at [FP, #-8], …).
//   The C caller therefore supplies at least 8 slots below FP.  Two extra
//   words sit at/above FP so the layout matches the saved FP+LR pad that
//   arg_disp skips after a real op_enter.  FP = &slots[8].
{
    memset(slots, 0, sizeof(ularge_t) * (8 + 2));
    return stack_p(slots + 8);
}


#if defined(__x86_64__) || defined(__amd64__)
static void invoke_amd64(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated x86-64 code with rbp = initial frame, r15 = flags
// ----------------------------------------------------------------------------
{
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn) code;
    alignas(16) ularge_t initial_frame[8 + 2];
    stack_p  frame = initial_jit_fp(initial_frame);
    uint64_t flags = rt.flags;
    (void) rt;
    asm volatile(
        "mov %1, %%rbp\n\t"
        "mov %2, %%r15\n\t"
        "call *%0"
        :
        : "r"(entry), "r"(frame), "r"(flags)
        : "rbp", "r15", "memory", "cc");
}
#endif


#if defined(__i386__) || defined(__i386)
static void invoke_i386(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated i386 code with ebp = initial frame, esi = flags
// ----------------------------------------------------------------------------
{
    typedef void (*entry_fn)(void);
    entry_fn  entry  = (entry_fn) code;
    alignas(16) ularge_t initial_frame[8 + 2];
    uintptr_t framep = uintptr_t(initial_jit_fp(initial_frame));
    uint32_t  flags  = uint32_t(rt.flags);
    (void) rt;
    asm volatile(
        "mov %1, %%ebp\n\t"
        "call *%0"
        :
        : "r"(entry), "r"(framep), "S"(flags)
        : "eax", "ecx", "edx", "ebp", "memory", "cc");
}
#endif


#if defined(__aarch64__)
static void invoke_arm64(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated AArch64 code; x29 = initial frame, x22 = flags
// ----------------------------------------------------------------------------
{
    alignas(16) ularge_t initial_frame[8 + 2];
    stack_p  frame = initial_jit_fp(initial_frame);
    uint64_t flags = rt.flags;
    (void) rt;
    // x29 is a reserved register on Darwin — save/restore it in the asm
    // rather than listing it as a clobber.
    asm volatile(
        "str x29, [sp, #-16]!\n\t"
        "mov x29, %0\n\t"
        "mov x22, %1\n\t"
        "mov x16, %2\n\t"
        "blr x16\n\t"
        "ldr x29, [sp], #16"
        :
        : "r"(frame), "r"(flags), "r"(code)
        : "x16", "x22", "x30", "memory", "cc");
}
#endif


#if defined(__arm__) && !defined(__aarch64__)
static void invoke_arm(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated 32-bit ARM code; r11 = initial frame, r10 = flags
// ----------------------------------------------------------------------------
{
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn) code;
    alignas(16) ularge_t initial_frame[8 + 2];
    stack_p  frame = initial_jit_fp(initial_frame);
    uint64_t flags = rt.flags;
    (void) rt;
    asm volatile(
        "mov r11, %0\n\t"
        "mov r10, %1"
        :
        : "r"(frame), "r"(flags)
        : "r10", "r11", "memory");
    entry();
}
#endif


#if defined(__riscv)
static void invoke_riscv64(runtime &rt, void *code)
// ----------------------------------------------------------------------------
//   Jump to generated RISC-V code; s0/fp = initial frame, s1 = flags.
// ----------------------------------------------------------------------------
//   Save ra and s2-s11 around the call: JIT uses s2–s11 as callee-saved
//   allocatable regs.  Do not list s2-s11 as clobbers — that starves GCC of
//   registers for the three "r" inputs.
{
    alignas(16) ularge_t initial_frame[8 + 2];
    stack_p  frame = initial_jit_fp(initial_frame);
    uint64_t flags = rt.flags;
    (void) rt;
    asm volatile(
        "mv s0, %[frame]\n\t"
        "mv s1, %[flags]\n\t"
        "mv t0, %[code]\n\t"
        "addi sp, sp, -96\n\t"
        "sd s2, 0(sp)\n\t"
        "sd s3, 8(sp)\n\t"
        "sd s4, 16(sp)\n\t"
        "sd s5, 24(sp)\n\t"
        "sd s6, 32(sp)\n\t"
        "sd s7, 40(sp)\n\t"
        "sd s8, 48(sp)\n\t"
        "sd s9, 56(sp)\n\t"
        "sd s10, 64(sp)\n\t"
        "sd s11, 72(sp)\n\t"
        "sd ra, 80(sp)\n\t"
        "jalr ra, t0, 0\n\t"
        "ld s2, 0(sp)\n\t"
        "ld s3, 8(sp)\n\t"
        "ld s4, 16(sp)\n\t"
        "ld s5, 24(sp)\n\t"
        "ld s6, 32(sp)\n\t"
        "ld s7, 40(sp)\n\t"
        "ld s8, 48(sp)\n\t"
        "ld s9, 56(sp)\n\t"
        "ld s10, 64(sp)\n\t"
        "ld s11, 72(sp)\n\t"
        "ld ra, 80(sp)\n\t"
        "addi sp, sp, 96"
        :
        : [frame] "r"(frame), [flags] "r"(flags), [code] "r"(code)
        : "s0", "s1", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
          "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
          "ra", "memory", "cc");
}
#endif


static bool native_run(runtime &rt, backend &be, byte_p start, byte_p end)
// ----------------------------------------------------------------------------
//   Translate bytecode and execute the generated native code
// ----------------------------------------------------------------------------
{
    xlr8_set_runtime(&rt);
    if (!be.translate(start, end))
        return false;

    section &code = be.code();
    size_t sz = code.size();
    if (!sz)
        return true;

    void *mem = map_executable(be, code.data(), sz);
    if (!mem)
        return false;

    rt.jit_code     = mem;
    rt.jit_map_size = sz;

    cstring dump    = runtime::dump_file;
    if (!dump)
        dump = getenv("XLR8_DUMP_JIT");
    if (dump)
        dump_jit_code(mem, sz, be, dump);
    xlr8_backend = &be;

#if defined(__x86_64__) || defined(__amd64__)
    invoke_amd64(rt, mem);
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

    unmap_executable(rt.jit_code, rt.jit_map_size);
    rt.jit_code = nullptr;
    rt.jit_map_size = 0;
    xlr8_backend = nullptr;
    return rt.error() == nullptr;
}


bool runtime::run_native(tok_t t)
// ----------------------------------------------------------------------------
//   Compile entry bytecode to native code and run it on the host CPU
// ----------------------------------------------------------------------------
{
    backend *be = backend::make(*this, backend::ARCH_NATIVE);
    if (!be)
    {
        error("No native backend for this CPU");
        return false;
    }

    size_t sz    = 0;
    byte_p start = byte_p(tok(t, &sz));
    byte_p end   = start + sz;

    bool   ok    = native_run(*this, *be, start, end);

    delete be;
    return ok;
}


uintptr_t xlr8_jit_addr(uintptr_t bc_pc)
// ----------------------------------------------------------------------------
//   Map a bytecode PC to a native code address
// ----------------------------------------------------------------------------
//   Only map if the bytecode is found in the range, otherwise leave as is
{
    if (!xlr8_backend)
        return bc_pc;
    uintptr_t addr = xlr8_backend->translated_address(byte_p(bc_pc));
    if (addr == hwcode_t(-1))
        return bc_pc;
    if (runtime *rt = xlr8_get_runtime())
        addr += uintptr_t(rt->jit_code);
    return addr;
}
