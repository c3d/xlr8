// ****************************************************************************
//  x86-emit.cpp                                                 XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Table-generated opcode emitters for x86/x86-64
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

EMIT(null)
// ----------------------------------------------------------------------------
//   Emit bytecode for the null opcode (inline data padding)
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_null()");
    e.error = "Translating null - Input bytecode is bogus";
    syscall(xlr8_null_opcode);
}


EMIT(user)
// ----------------------------------------------------------------------------
//   Emit a user-defined opcode (128..255, or registered low opcodes)
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_user(%lld, %p)", e.op, &e.pc);
    if (runtime::user_fn user = rt.user_opcode(e.op))
    {
        uintptr_t pc = uintptr_t(e.pc);
        if (uintptr_t fnaddr = user(rt, &e))
        {
            hw_sys_arg_respill();
            hw_sys_imm(0, pc);
            call_ptr(xlr8_get_runtime_with_pc);
            if (ptr64())
                emit_mov_rr(HW_RDI, HW_RAX);
            hw_sys_imm(1, 0);

            user = runtime::user_fn(fnaddr);
            call_ptr(user);
            return;
        }
    }
    record(x86_ops, "No user opcode for %u", e.op);
    syscall(xlr8_user_opcode);
}


EMIT(undef)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_undef(%llu)", e.tok);
    e.error = "Translating undef";
    syscall(xlr8_undef_opcode);
}


EMIT(end)
// ----------------------------------------------------------------------------
//   Emit bytecode for the end opcode (stop translating this range)
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_end");
    if (ptr64())
        hw_sys_arg_reload();
    e.pc = e.end;
}


EMIT(int)
// ----------------------------------------------------------------------------
//   Emit bytecode for the int opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_int(%u, %lld)", e.dst, e.kint);
    store_imm(e.dst, e.kint);
}


EMIT(nat)
// ----------------------------------------------------------------------------
//   Emit bytecode for the nat opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_nat(%u, %llu)", e.dst, e.knat);
    store_imm(e.dst, value_t(e.knat));
}


EMIT(flt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the flt opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_flt(%u, %g)", e.dst, e.kflt);
    store_float(e.dst, e.kflt);
}


EMIT(dbl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbl opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dbl(%u, %g)", e.dst, e.kdbl);
    store_double(e.dst, e.kdbl);
}


EMIT(txt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the txt opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_txt(%u, %llu)", e.dst, e.tok);
    store_imm(e.dst, value_t(e.tok));
}


EMIT(copy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the copy opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_copy(%u, %u)", e.dst, e.src);
    mov_local(e.dst, e.src);
}


EMIT(swap)
// ----------------------------------------------------------------------------
//   Emit bytecode for the swap opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_swap(%u, %u)", e.dst, e.src);
    swap_locals(e.dst, e.src);
}


EMIT(ccopy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ccopy opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ccopy %u? %u=%u)", e.pred, e.dst, e.src);
    cond_mov_local(e.pred, e.dst, e.src);
}


EMIT(select)
// ----------------------------------------------------------------------------
//   Emit bytecode for the select opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_select %u = %u ? %u : %u",
           e.dst, e.pred, e.src, e.xtra);
    select_local(e.pred, e.dst, e.src, e.xtra);
}


EMIT(jmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the jmp opcode
// ----------------------------------------------------------------------------
{
    hw_sys_arg_reload();
    hwcode_t site = emit_near_jmp_rel32();
    rel32_function(e.tok, site);
    record(x86_ops, "emit_jump %llu", e.tok);
}


EMIT(call)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode
// ----------------------------------------------------------------------------
//   XLR8-to-XLR8 calls pass arguments in local homes (iorder), not SysV.
//   Callee enter/leave saves and restores those homes.  Without bytecode
//   enter, the return address on x86 is already on the stack from `call`,
//   but FP must be saved/restored if there is no enclosing enter frame.
{
    record(x86_ops, "emit_call %llu", e.tok);
    if (ptr64())
    {
        hw_sys_arg_reload();

        // Always push rbp: (a) the callee's enter will overwrite it,
        // and (b) the push keeps callee entry 16-byte aligned
        // (push 8 + call 8 = 16 bytes from aligned RSP).
        emit_push_reg(HW_FRAME);
        emit8(0xE8);
        hwcode_t site = cursor();
        emit32le(0);
        rel32_function(e.tok, site);
        emit_pop_reg(HW_FRAME);

        x86reg_t r0 = hw_int_order(0);
        if (r0 != HW_RAX)
            emit_mov_rr(r0, HW_RAX);
    }
    else
    {
        hw_sys_arg_reload();
        emit_push_reg(HW_FRAME);
        emit8(0xE8);
        hwcode_t site = cursor();
        emit32le(0);
        rel32_function(e.tok, site);
        emit_pop_reg(HW_FRAME);

        x86reg_t r0 = hw_int_order_32(0);
        if (r0 != HW_RAX)
            emit_mov_rr(r0, HW_RAX);
    }
}


EMIT(ijmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
//   Unlike jmp, ijmp does NOT pop callee-saved regs: the target may be a
//   mid-function label (no prologue).  Stack balance is maintained because
//   the eventual end/epilogue in the target function pops the set.
//   `pca` stores a bytecode PC; resolve to JIT via xlr8_jit_addr (amd64+i386).
{
    record(x86_ops, "emit_ijmp %u", e.src);
    hw_sys_arg_reload();
    hw_sys_arg(0, e.src);
    syscall(reinterpret_cast<void (*)()>(xlr8_jit_addr));
    hw_sys_arg_reload();
    emit_jmp_rr(HW_RAX);
}


EMIT(icall)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_icall %u", e.src);
    hw_sys_arg_reload();
    hw_sys_arg(0, e.src);
    syscall(reinterpret_cast<void (*)()>(xlr8_jit_addr));
    // JIT address now in rax; reload spills then call.
    hw_sys_arg_reload();
    bool save_fp = !pre_enter_frame_size;
    if (save_fp)
        emit_push_reg(HW_FRAME);
    emit_call_rr(HW_RAX);
    if (save_fp)
        emit_pop_reg(HW_FRAME);
}


EMIT(ret)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ret opcode
// ----------------------------------------------------------------------------
//   Local 0 is the XLR8 return value (already in iorder[0]).  Mirror it
//   into rax for any host that inspects the C return register.
//   On x86, the return address is on the stack (not in a link register), so
//   we must pop the callee-saved regs pushed by the prologue via epilogue().
{
    if (ptr64())
    {
        hw_sys_arg_reload();
        x86reg_t src = hw_int_order(0);
        if (src != HW_RAX)
            emit_mov_rr(HW_RAX, src);
        epilogue();
    }
    else
    {
        hw_sys_arg_reload();
        x86reg_t src = hw_int_order_32(0);
        if (src != HW_RAX)
            emit_mov_rr(HW_RAX, src);
        epilogue();
    }
}


EMIT(pca)
// ----------------------------------------------------------------------------
//   Emit PC-relative code address; queue target for JIT translation
// ----------------------------------------------------------------------------
{
    byte_p   target = e.bpc + e.broff;
    x86reg_t r      = hw_local(e.dst);
    record(x86_ops, "emit_pca %u = %p", e.dst, target);
    branch(target, hwcode_t(-1));
    set_imm(r, uint64_t(target));
}


EMIT(pcd)
// ----------------------------------------------------------------------------
//   Emit PC-relative data address; do not translate the target as code
// ----------------------------------------------------------------------------
{
    byte_p   target = e.bpc + e.broff;
    x86reg_t r      = hw_local(e.dst);
    record(x86_ops, "emit_pcd %u = %p", e.dst, target);
    set_imm(r, uint64_t(target));
}


EMIT(enter)
// ----------------------------------------------------------------------------
//   Emit bytecode for the enter opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_enter %u", e.dst);
    if (ptr64())
        hw_sys_arg_reload();

    uint frame = e.dst + 1;
    if (pre_enter_frame_size)
        record(x86_error, "Enter called twice in a function, %u after %u",
               frame, local_frame_size);
    pre_enter_frame_size = local_frame_size;
    local_frame_size     = frame;
    frame_size(frame);

    if (!ptr64())
    {
        // Emit space for high 32-bits of flags
        emit_xor_rr(HW_RAX, HW_RAX);
        emit_push_reg(HW_RAX);
    }
    emit_push_reg(HW_FRAME);
    emit_mov_rr(HW_FRAME, HW_RSP);

    if (ptr64())
    {
        // Allocate the full frame below rbp (shadows + high locals).
        // push rbp consumed 8 bytes; total must be 16-byte aligned
        // so that SP is ready for C calls (SysV requires 16-byte alignment
        // before call, i.e. SP ≡ 0 mod 16 at the call instruction).
        int32_t total = int32_t(8 + frame * sizeof(ularge_t));
        total = (total + 15) & ~15;
        int32_t alloc = total - 8;
        if (alloc > 0)
            emit_sub_rsp_imm(alloc);

        // Save caller's local homes (iorder[i]) into frame slots, mirroring
        // arm64: slot i holds iorder[i] so get/set stay consistent.
        uint shadows = frame;
        if (shadows > HW_SHADOWS)
            shadows = HW_SHADOWS;
        for (uint i = 0; i < shadows; i++)
            emit_store_u64(HW_FRAME, hw_int_order(i), local_disp(i));
    }
    else
    {
        int32_t bytes = int32_t(e.dst * sizeof(ularge_t));
        emit_sub_rsp_imm(bytes);

        uint shadows = frame;
        if (shadows > HW_SHADOWS_32)
            shadows = HW_SHADOWS_32;
        for (uint i = 0; i < shadows; i++)
            emit_store_u64(HW_FRAME, hw_int_order_32(i), local_disp(i));
    }
}


EMIT(leave)
// ----------------------------------------------------------------------------
//   Emit bytecode for the leave opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_leave");
    if (ptr64())
        hw_sys_arg_reload();

    if (ptr64())
    {
        uint frame = local_frame_size;
        if (!frame)
            record(x86_error, "Running leave without having run enter");
        uint shadows = frame;
        if (shadows > HW_SHADOWS)
            shadows = HW_SHADOWS;
        for (uint i = 0; i < shadows; i++)
            emit_load_u64(hw_int_order(i), HW_FRAME, local_disp(i));
    }

    if (!ptr64())
    {
        uint frame = local_frame_size;
        if (!frame)
            record(x86_error, "Running leave without having run enter");
        uint shadows = frame;
        if (shadows > HW_SHADOWS_32)
            shadows = HW_SHADOWS_32;
        for (uint i = 0; i < shadows; i++)
            emit_load_u64(hw_int_order_32(i), HW_FRAME, local_disp(i));
    }

    emit_mov_rr(HW_RSP, HW_FRAME);
    emit_pop_reg(HW_FRAME);
    if (!ptr64())
    {
        emit_pop_reg(HW_RAX);
        local_frame_size     = pre_enter_frame_size;
        pre_enter_frame_size = 0;
        forget_registers();
    }
    else
    {
        local_frame_size     = pre_enter_frame_size;
        pre_enter_frame_size = 0;
    }
}


EMIT(get)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    x86reg_t dr = hw_dst_local(e.dst);
    if (ptr64() && e.src < HW_SHADOWS && e.src < local_frame_size)
    {
        emit_load_u64(dr, HW_FRAME, local_disp(e.src));
        record(x86_ops, "emit_get [%u] = %u reg %u local %d",
               e.dst, e.src, dr, local_disp(e.src));
    }
    else if (ptr64())
    {
        emit_load_u64(HW_RAX, HW_FRAME, 0);
        emit_load_u64(dr, HW_RAX, local_disp(e.src));
        record(x86_ops, "emit_get [%u] = %u reg %u parent %d",
               e.dst, e.src, dr, local_disp(e.src));
    }
    else if (e.src < HW_SHADOWS_32 && e.src < local_frame_size)
    {
        emit_load_u64(dr, HW_FRAME, local_disp(e.src));
        record(x86_ops, "emit_get [%u] = %u reg %u local %d",
               e.dst, e.src, dr, local_disp(e.src));
    }
    else
    {
        emit_load_u64(HW_RAX, HW_FRAME, 0);
        emit_load_u64(dr, HW_RAX, local_disp(e.src));
        record(x86_ops, "emit_get [%u] = %u reg %u parent %d",
               e.dst, e.src, dr, local_disp(e.src));
    }
}


EMIT(set)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    x86reg_t sr = hw_local(e.src);
    if (ptr64() && e.dst < HW_SHADOWS && e.dst < local_frame_size)
    {
        emit_store_u64(HW_FRAME, sr, local_disp(e.dst));
        record(x86_ops, "emit_set %u = [%u] reg %u local %d",
               e.dst, e.src, sr, local_disp(e.dst));
    }
    else if (ptr64())
    {
        emit_load_u64(HW_RAX, HW_FRAME, 0);
        emit_store_u64(HW_RAX, sr, local_disp(e.dst));
        record(x86_ops, "emit_set %u = [%u] reg %u parent %d",
               e.dst, e.src, sr, local_disp(e.dst));
    }
    else if (e.dst < HW_SHADOWS_32 && e.dst < local_frame_size)
    {
        emit_store_u64(HW_FRAME, sr, local_disp(e.dst));
        record(x86_ops, "emit_set %u = [%u] reg %u local %d",
               e.dst, e.src, sr, local_disp(e.dst));
    }
    else
    {
        emit_load_u64(HW_RAX, HW_FRAME, 0);
        emit_store_u64(HW_RAX, sr, local_disp(e.dst));
        record(x86_ops, "emit_set %u = [%u] reg %u parent %d",
               e.dst, e.src, sr, local_disp(e.dst));
    }
}


EMIT(push)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    if (ptr64())
        hw_sys_arg_reload();
    x86reg_t sr  = hw_local(e.src);
    record(x86_ops, "emit_push %u reg %u", e.src, sr);
    emit_push_reg(sr);
}


EMIT(pop)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    if (ptr64())
        hw_sys_arg_reload();
    x86reg_t dr  = hw_dst_local(e.dst);
    record(x86_ops, "emit_pop %u reg %u", e.dst, dr);
    emit_pop_reg(dr);
}


EMIT(sys)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sys opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_sys %llu args=%p", e.tok, e.pc);
    dispatch_sys(e);
}


EMIT(sto)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sto opcode
// ----------------------------------------------------------------------------
{
    x86reg_t r   = hw_local(e.src);
    size_t   off = e.dst * sizeof(value_t);
    set_imm(HW_RAX, uintptr_t(&xlr8_globals));
    emit_load_u64(HW_RAX, HW_RAX);
    emit_store_u64(HW_RAX, r, int(off));
    record(x86_ops, "store_global g[%u]=%u off %zu reg %u",
           e.dst, e.src, off, r);
}


EMIT(rcl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    x86reg_t r   = hw_dst_local(e.dst);
    size_t   off = e.src * sizeof(value_t);
    set_imm(HW_RAX, uintptr_t(&xlr8_globals));
    emit_load_u64(HW_RAX, HW_RAX);
    emit_load_u64(r, HW_RAX, int(off));
    record(x86_ops, "load_global g[%u]=%u off %zu reg %u",
           e.dst, e.src, off, r);
}


EMIT(dbra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
//   Overflow locals live in temps.  The JNZ may re-enter a loop body without
//   running post-opcode register_sync, so write the decremented temp back to
//   the frame before the branch (durable homes are updated in place).
{
    x86reg_t r = hw_local(e.dst);
    record(x86_ops, "emit_dbra %u, %lld reg %u", e.dst, e.broff, r);
    if (ptr64())
        hw_sys_arg_reload(e.bpc + e.broff);
    emit_grp5_ff(r, 0xc8); // Decr — must be last before JNZ (flags)
    if (r >= HW_TEMP0 && r < HW_TEMP0 + HW_TEMPS)
        store(e.dst, r);
    hwcode_t pos = emit_jcc_rel32(JCC_JNZ);
    rel32_branch(e.bpc + e.broff, pos);
}


EMIT(bra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bra opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_bra %lld + %lld -> %lld",
           e.bpc, e.broff, e.bpc+e.broff);
    if (ptr64())
        hw_sys_arg_reload(e.bpc + e.broff);
    hwcode_t site = emit_near_jmp_rel32();
    rel32_branch(e.bpc + e.broff, site);
    e.pc = e.end;               // Pick up next translation spot
}


EMIT(btrue)
// ----------------------------------------------------------------------------
//   Emit bytecode for the btrue opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_btrue(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.bpc + e.broff, true);
}


EMIT(bfalse)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bfalse opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_bfalse(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.bpc + e.broff, false);
}


EMIT(beq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the beq opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_beq(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, JCC_JZ);
}


EMIT(bne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bne opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_bne(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, JCC_JNZ);
}


EMIT(blt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the blt opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_blt(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, JCC_JL);
}


EMIT(bgt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bgt opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_bgt(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, JCC_JG);
}


EMIT(ble)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ble opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ble(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, JCC_JLE);
}


EMIT(bge)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bge opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_bge(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, JCC_JGE);
}


EMIT(switch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the switch opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_switch(%u)", e.src);
    dispatch(e.src, e.pc, JCC_JZ);
}


EMIT(dispatch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dispatch opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dispatch(%u)", e.src);
    dispatch(e.src, e.pc, JCC_JGE);
}


EMIT(true)
// ----------------------------------------------------------------------------
//   Emit bytecode for the true opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_true(%u)", e.pred);
    set_pred(e.pred, true);
}


EMIT(false)
// ----------------------------------------------------------------------------
//   Emit bytecode for the false opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_false(%u)", e.pred);
    set_pred(e.pred, false);
}


EMIT(cmpeq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeq opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpeq(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, SETCC_EQ);
}


EMIT(cmpne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpne opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpne(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, SETCC_NE);
}


EMIT(cmplt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplt opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmplt(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, SETCC_LT);
}


EMIT(cmple)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmple opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmple(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, SETCC_LE);
}


EMIT(cmpult)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpult opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpult(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, SETCC_LTU);
}


EMIT(cmpule)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpule opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpule(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, SETCC_LEU);
}


EMIT(cmpeqi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeqi opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpeqi(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, SETCC_EQ);
}


EMIT(cmpnei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpnei opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpnei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, SETCC_NE);
}


EMIT(cmplti)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplti opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmplti(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, SETCC_LT);
}


EMIT(cmplei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplei opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmplei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, SETCC_LE);
}


EMIT(cmpequ)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpequ opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpequ(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, SETCC_EQ);
}


EMIT(cmpneu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpneu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpneu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, SETCC_NE);
}


EMIT(cmpltu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpltu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpltu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, SETCC_LTU);
}


EMIT(cmpleu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpleu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_cmpleu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, SETCC_LEU);
}


EMIT(pushf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pushf opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_pushf()");
    x86reg_t flags = hw_flags();
    if (!ptr64())
    {
        emit_load_u32(HW_RAX, HW_FRAME, 4);
        emit_push_reg(HW_RAX);
    }
    emit_push_reg(flags);
}


EMIT(popf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the popf opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_popf()");
    x86reg_t flags = hw_flags();
    emit_pop_reg(flags);
    if (!ptr64())
    {
        emit_pop_reg(HW_RAX);
        emit_store_u32(HW_FRAME, HW_RAX, 4);
    }
}


EMIT(add)
// ----------------------------------------------------------------------------
//   Emit bytecode for the add opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_add(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_add);
}


EMIT(sub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sub opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_sub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_sub);
}


EMIT(mul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mul opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_mul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_mul);
}


EMIT(umul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umul opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_umul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_umul);
}


EMIT(div)
// ----------------------------------------------------------------------------
//   Emit bytecode for the div opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_div(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_div);
}


EMIT(udiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the udiv opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_udiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_udiv);
}


EMIT(mod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mod opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_mod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_mod);
}


EMIT(umod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umod opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_umod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_umod);
}


EMIT(and)
// ----------------------------------------------------------------------------
//   Emit bytecode for the and opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_and(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_and);
}


EMIT(or)
// ----------------------------------------------------------------------------
//   Emit bytecode for the or opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_or(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_or);
}


EMIT(xor)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xor opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_xor(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_xor);
}


EMIT(shl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shl opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_shl(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_shl);
}


EMIT(shr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shr opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_shr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_shr);
}


EMIT(ushr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushr opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ushr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_ushr);
}


EMIT(neg)
// ----------------------------------------------------------------------------
//   Emit bytecode for the neg opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_neg(%u)", e.dst);
    neg_local(e.dst);
}


EMIT(not)
// ----------------------------------------------------------------------------
//   Emit bytecode for the not opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_not(%u)", e.dst);
    not_local(e.dst);
}


EMIT(incr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the incr opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_incr(%u)", e.dst);
    incr_local(e.dst);
}


EMIT(decr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the decr opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_decr(%u)", e.dst);
    decr_local(e.dst);
}


EMIT(addi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addi opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_addi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_addi);
}


EMIT(addu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_addu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_addu);
}


EMIT(subi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subi opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_subi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_subi);
}


EMIT(subu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_subu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_subu);
}


EMIT(muli)
// ----------------------------------------------------------------------------
//   Emit bytecode for the muli opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_muli(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_muli);
}


EMIT(mulu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mulu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_mulu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_mulu);
}


EMIT(divi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divi opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_divi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_divi);
}


EMIT(divu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_divu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_divu);
}


EMIT(modi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modi opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_modi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_modi);
}


EMIT(modu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_modu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_modu);
}


EMIT(andu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the andu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_andu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_andu);
}


EMIT(oru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the oru opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_oru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_oru);
}


EMIT(xoru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xoru opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_xoru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_xoru);
}


EMIT(shlu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shlu opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_shlu(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_shlu);
}


EMIT(shru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shru opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_shru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_shru);
}


EMIT(ushru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushru opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ushru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_ushru);
}


EMIT(st8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st8(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 8);
}


EMIT(st4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st4(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 4);
}


EMIT(st2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st2(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 2);
}


EMIT(st1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st1(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 1);
}


EMIT(ld8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld8(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 8);
}


EMIT(ld4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld4(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 4);
}


EMIT(ld2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld2(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 2);
}


EMIT(ld1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1 opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld1(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 1);
}


EMIT(st8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 8, e.kint);
}


EMIT(st4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 4, e.kint);
}


EMIT(st2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 2, e.kint);
}


EMIT(st1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_st1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 1, e.kint);
}


EMIT(ld8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 8, e.kint);
}


EMIT(ld4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 4, e.kint);
}


EMIT(ld2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 2, e.kint);
}


EMIT(ld1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1o opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ld1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 1, e.kint);
}


EMIT(lds)
// ----------------------------------------------------------------------------
//   Emit bytecode for the lds opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_lds%u %u=[%u]", e.kshift, e.dst, e.src);
    load_sext(e.dst, e.src, e.kshift);
}


EMIT(ldos)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ldos opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ldos%u %u=[%u+%lld]", e.kshift, e.dst, e.src, e.kint);
    load_sext(e.dst, e.src, e.kshift, e.kint);
}


EMIT(sext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sext opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_sext(%u, %u)", e.dst, e.kshift);
    sext_local(e.dst, e.kshift);
}


EMIT(zext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the zext opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_zext(%u, %u)", e.dst, e.kshift);
    zext_local(e.dst, e.kshift);
}


EMIT(fadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fadd opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_fadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fadd, 32);
}


EMIT(fsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_fsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fsub, 32);
}


EMIT(fmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_fmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fmul, 32);
}


EMIT(fdiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_fdiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fdiv, 32);
}


EMIT(dadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dadd, 64);
}


EMIT(dsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dsub, 64);
}


EMIT(dmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dmul, 64);
}


EMIT(ddiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ddiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_ddiv, 64);
}


// ============================================================================
//
//   Sys and native opcodes (native.tbl)
//
// ============================================================================

// x86 SYS emitters covering native.tbl — mirrors arm64 CALL/ARG/IMM/RET.
// On amd64, ARG/IMM spill caller-saved durable homes lazily; syscall() just
// does call_ptr(fn) (no spill_all).  On i386, bodies fall through to the
// old spill_all + cdecl path.

#define SYS(op) void x86::emit_sys_##op(emit_args &e)
#define ARG(a, loc)   hw_sys_arg(a, loc);
#define IMM(a, val)   hw_sys_imm(a, val);
#define FARG(a, loc)  hw_sys_arg_fp(a, loc, false);
#define DARG(a, loc)  hw_sys_arg_fp(a, loc, true);
#define RET(loc)      hw_sys_ret(loc);
#define SRET(loc,sz)  hw_sys_ret(loc, sz);
#define FRET(loc)     hw_sys_ret_fp(loc, false);
#define DRET(loc)     hw_sys_ret_fp(loc, true);
#define NORET         /* no output */

#define CALL(fn, inputs, outputs)                   \
    do                                              \
    {                                               \
        hw_sys_arg_respill();                       \
        inputs;                                     \
        syscall(reinterpret_cast<void (*)()>(fn));  \
        outputs;                                    \
    } while (0)

SYS(globals)
// ----------------------------------------------------------------------------
//   Resize globals table to given length
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_globals, IMM(0, e.klen), NORET);
}

SYS(malloc)
// ----------------------------------------------------------------------------
//   Allocate heap memory of given size
// ----------------------------------------------------------------------------
{
    CALL(::malloc, ARG(0, e.src), RET(e.dst));
}

SYS(kmalloc)
// ----------------------------------------------------------------------------
//   Allocate heap memory of constant size
// ----------------------------------------------------------------------------
{
    CALL(::malloc, IMM(0, e.klen), RET(e.dst));
}

SYS(realloc)
// ----------------------------------------------------------------------------
//   Resize heap allocation
// ----------------------------------------------------------------------------
{
    CALL(::realloc, ARG(0, e.dst) ARG(1, e.src), RET(e.dst));
}

SYS(free)
// ----------------------------------------------------------------------------
//   Free heap allocation
// ----------------------------------------------------------------------------
{
    CALL(::free, ARG(0, e.dst), NORET);
}

SYS(alloca)
// ----------------------------------------------------------------------------
//   Allocate stack space and return pointer
// ----------------------------------------------------------------------------
{
    if (ptr64())
        hw_sys_arg_reload();

    int align = ptr64() ? 16 : 8;
    int64_t bytes = int64_t(e.kint);
    if (bytes > 0)
        bytes = (bytes + align - 1) & ~int64_t(align - 1);
    else
        bytes = bytes & ~int64_t(align - 1);

    if (bytes > 0)
        emit_sub_rsp_imm(int32_t(bytes));
    else if (bytes < 0)
        emit_add_rsp_imm(int32_t(-bytes));
    emit_mov_rr(hw_dst_local(e.dst), HW_RSP);
}

SYS(valloca)
// ----------------------------------------------------------------------------
//   Allocate variable-size stack space, return pointer
// ----------------------------------------------------------------------------
{
    if (ptr64())
        hw_sys_arg_reload();

    x86reg_t sz = hw_local(e.src);
    x86reg_t t  = HW_RAX;
    emit_mov_rr(t, sz);
    int align = ptr64() ? 16 : 8;
    set_imm(HW_RCX, uint64_t(align - 1));
    emit_mov_rr(HW_RDX, t);
    emit_add_rr(HW_RDX, HW_RCX);          // size + (align-1)
    emit_test_rr(t, t);
    hwcode_t jle = emit_jcc_rel8(JCC_JLE); // size <= 0 → keep t
    emit_mov_rr(t, HW_RDX);
    patch(jle, cursor());
    set_imm(HW_RCX, ~uint64_t(align - 1));
    emit_and_rr(t, HW_RCX);
    emit_sub_rr(HW_RSP, t);
    emit_mov_rr(hw_dst_local(e.dst), HW_RSP);
}

SYS(memcpy)
// ----------------------------------------------------------------------------
//   Copy memory region
// ----------------------------------------------------------------------------
{
    CALL(::memcpy, ARG(0, e.dst) ARG(1, e.src) ARG(2, e.len), NORET);
}

SYS(memmove)
// ----------------------------------------------------------------------------
//   Move memory region (overlap-safe)
// ----------------------------------------------------------------------------
{
    CALL(::memmove, ARG(0, e.dst) ARG(1, e.src) ARG(2, e.len), NORET);
}

SYS(memset)
// ----------------------------------------------------------------------------
//   Fill memory with byte from register
// ----------------------------------------------------------------------------
{
    CALL(::memset, ARG(0, e.dst) ARG(1, e.src) ARG(2, e.len), NORET);
}

SYS(kmemcpy)
// ----------------------------------------------------------------------------
//   Copy memory region of constant length
// ----------------------------------------------------------------------------
{
    CALL(::memcpy, ARG(0, e.dst) ARG(1, e.src) IMM(2, e.klen), NORET);
}

SYS(kmemmove)
// ----------------------------------------------------------------------------
//   Move memory region of constant length
// ----------------------------------------------------------------------------
{
    CALL(::memmove, ARG(0, e.dst) ARG(1, e.src) IMM(2, e.klen), NORET);
}

SYS(kmemset)
// ----------------------------------------------------------------------------
//   Fill memory with byte, constant length
// ----------------------------------------------------------------------------
{
    CALL(::memset, ARG(0, e.dst) ARG(1, e.src) IMM(2, e.klen), NORET);
}

SYS(memkset)
// ----------------------------------------------------------------------------
//   Fill memory with constant byte value
// ----------------------------------------------------------------------------
{
    CALL(::memset, ARG(0, e.dst) IMM(1, uint64_t(e.kint)); ARG(2, e.src), NORET);
}

SYS(kmemkset)
// ----------------------------------------------------------------------------
//   Fill memory with constant byte and length
// ----------------------------------------------------------------------------
{
    CALL(::memset, ARG(0, e.dst) IMM(1, uint64_t(e.kint)); IMM(2, e.klen), NORET);
}

SYS(memcmp)
// ----------------------------------------------------------------------------
//   Compare two memory regions
// ----------------------------------------------------------------------------
{
    CALL(::memcmp, ARG(0, e.src) ARG(1, e.xtra) ARG(2, e.len), SRET(e.dst, 32));
}

SYS(kmemcmp)
// ----------------------------------------------------------------------------
//   Compare memory regions of constant length
// ----------------------------------------------------------------------------
{
    CALL(::memcmp, ARG(0, e.src) ARG(1, e.xtra) IMM(2, e.klen), SRET(e.dst, 32));
}

SYS(memchr)
// ----------------------------------------------------------------------------
//   Find byte in memory region
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr),
         ARG(0, e.src) ARG(1, e.ival) ARG(2, e.len), RET(e.dst));
}

SYS(kmemchr)
// ----------------------------------------------------------------------------
//   Find byte in memory of constant length
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr),
         ARG(0, e.src) ARG(1, e.ival) IMM(2, e.klen), RET(e.dst));
}

SYS(memkchr)
// ----------------------------------------------------------------------------
//   Find constant byte in memory region
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr),
         ARG(0, e.src) IMM(1, uint64_t(e.kint)); ARG(2, e.len), RET(e.dst));
}

SYS(kmemkchr)
// ----------------------------------------------------------------------------
//   Find constant byte in memory of constant length
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr),
         ARG(0, e.src) IMM(1, uint64_t(e.kint)); IMM(2, e.klen), RET(e.dst));
}

SYS(strlen)
// ----------------------------------------------------------------------------
//   Compute length of C string
// ----------------------------------------------------------------------------
{
    CALL(::strlen, ARG(0, e.src), RET(e.dst));
}

SYS(strnlen)
// ----------------------------------------------------------------------------
//   Compute bounded length of C string
// ----------------------------------------------------------------------------
{
    CALL(::strnlen, ARG(0, e.src) ARG(1, e.len), RET(e.dst));
}

SYS(kstrnlen)
// ----------------------------------------------------------------------------
//   Compute C string length with constant bound
// ----------------------------------------------------------------------------
{
    CALL(::strnlen, ARG(0, e.src) IMM(1, e.klen), RET(e.dst));
}

SYS(strcpy)
// ----------------------------------------------------------------------------
//   Copy C string
// ----------------------------------------------------------------------------
{
    CALL(::strcpy, ARG(0, e.dst) ARG(1, e.src), RET(e.dst));
}

SYS(strncpy)
// ----------------------------------------------------------------------------
//   Copy C string with length bound
// ----------------------------------------------------------------------------
{
    CALL(::strncpy, ARG(0, e.dst) ARG(1, e.src) ARG(2, e.len), RET(e.dst));
}

SYS(kstrncpy)
// ----------------------------------------------------------------------------
//   Copy C string with constant length bound
// ----------------------------------------------------------------------------
{
    CALL(::strncpy, ARG(0, e.dst) ARG(1, e.src) IMM(2, e.klen), RET(e.dst));
}

SYS(strcat)
// ----------------------------------------------------------------------------
//   Concatenate C strings
// ----------------------------------------------------------------------------
{
    CALL(::strcat, ARG(0, e.dst) ARG(1, e.src), RET(e.dst));
}

SYS(strncat)
// ----------------------------------------------------------------------------
//   Concatenate C strings with length bound
// ----------------------------------------------------------------------------
{
    CALL(::strncat, ARG(0, e.dst) ARG(1, e.src) ARG(2, e.len), RET(e.dst));
}

SYS(kstrncat)
// ----------------------------------------------------------------------------
//   Concatenate C strings with constant bound
// ----------------------------------------------------------------------------
{
    CALL(::strncat, ARG(0, e.dst) ARG(1, e.src) IMM(2, e.klen), RET(e.dst));
}

SYS(strcmp)
// ----------------------------------------------------------------------------
//   Compare two C strings
// ----------------------------------------------------------------------------
{
    CALL(::strcmp, ARG(0, e.src) ARG(1, e.xtra), SRET(e.dst, 32));
}

SYS(strncmp)
// ----------------------------------------------------------------------------
//   Compare C strings with length bound
// ----------------------------------------------------------------------------
{
    CALL(::strncmp, ARG(0, e.src) ARG(1, e.xtra) ARG(2, e.len), SRET(e.dst, 32));
}

SYS(kstrncmp)
// ----------------------------------------------------------------------------
//   Compare C strings with constant bound
// ----------------------------------------------------------------------------
{
    CALL(::strncmp,
         ARG(0, e.src) ARG(1, e.xtra) IMM(2, e.klen), SRET(e.dst, 32));
}

SYS(strchr)
// ----------------------------------------------------------------------------
//   Find character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*)(char *, int)>(::strchr),
         ARG(0, e.src) ARG(1, e.ival), RET(e.dst));
}

SYS(kstrchr)
// ----------------------------------------------------------------------------
//   Find constant character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*)(char *, int)>(::strchr),
         ARG(0, e.src) IMM(1, uint64_t(e.kint)), RET(e.dst));
}

SYS(strrchr)
// ----------------------------------------------------------------------------
//   Find last character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*)(char *, int)>(::strrchr),
         ARG(0, e.src) ARG(1, e.ival), RET(e.dst));
}

SYS(kstrrchr)
// ----------------------------------------------------------------------------
//   Find last constant character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*)(char *, int)>(::strrchr),
         ARG(0, e.src) IMM(1, uint64_t(e.kint)), RET(e.dst));
}

SYS(strstr)
// ----------------------------------------------------------------------------
//   Find substring in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*)(char *, const char *)>(::strstr),
         ARG(0, e.src) ARG(1, e.xtra), RET(e.dst));
}

#if 0 // does not exist on Debian GNU/Linux 12 (bookworm)
SYS(strnstr)
// ----------------------------------------------------------------------------
//   Find substring in C string with length bound
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*)(char *, const char *)>(::strstr),
         ARG(0, e.src) ARG(1, e.xtra), RET(e.dst));
}

SYS(kstrnstr)
// ----------------------------------------------------------------------------
//   Find substring with constant length bound
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*)(char *, const char *)>(::strstr),
         ARG(0, e.src) ARG(1, e.xtra), RET(e.dst));
}

#endif // Debian

SYS(strcasestr)
// ----------------------------------------------------------------------------
//   Find substring case-insensitively
// ----------------------------------------------------------------------------
{
#if defined(__GLIBC__)
    CALL(static_cast<char *(*)(char *, const char *)>(::strcasestr),
         ARG(0, e.src) ARG(1, e.xtra), RET(e.dst));
#else
    CALL(static_cast<char *(*)(const char *, const char *)>(::strcasestr),
         ARG(0, e.src) ARG(1, e.xtra), RET(e.dst));
#endif
}

SYS(strdup)
// ----------------------------------------------------------------------------
//   Duplicate C string on heap
// ----------------------------------------------------------------------------
{
    CALL(::strdup, ARG(0, e.src), RET(e.dst));
}

SYS(strndup)
// ----------------------------------------------------------------------------
//   Duplicate C string on heap with length bound
// ----------------------------------------------------------------------------
{
    CALL(::strndup, ARG(0, e.src) ARG(1, e.len), RET(e.dst));
}

SYS(kstrndup)
// ----------------------------------------------------------------------------
//   Duplicate C string with constant length bound
// ----------------------------------------------------------------------------
{
    CALL(::strndup, ARG(0, e.src) IMM(1, e.klen), RET(e.dst));
}

SYS(cmdline)
// ----------------------------------------------------------------------------
//   Get command-line argument by index
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_argv, ARG(0, e.src), RET(e.dst));
}

SYS(env)
// ----------------------------------------------------------------------------
//   Get environment variable by index
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_env, ARG(0, e.src), RET(e.dst));
}

SYS(infix)
// ----------------------------------------------------------------------------
//   Enter infix parsing mode
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_infix, /* no input */, NORET);
}

SYS(prefix)
// ----------------------------------------------------------------------------
//   Enter prefix parsing mode
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_prefix, /* no input */, NORET);
}

SYS(postfix)
// ----------------------------------------------------------------------------
//   Enter postfix parsing mode
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_postfix, /* no input */, NORET);
}

SYS(block)
// ----------------------------------------------------------------------------
//   Enter block parsing mode
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_block, /* no input */, NORET);
}

SYS(builtin)
// ----------------------------------------------------------------------------
//   Switch to builtin (non-parsing) mode
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_builtin, /* no input */, NORET);
}

SYS(parsing)
// ----------------------------------------------------------------------------
//   Switch to parsing mode
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_parsing, /* no input */, NORET);
}

SYS(branch_on_eof)
// ----------------------------------------------------------------------------
//   Branch if input stream is at EOF
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_stream_eof,
         IMM(0, e.knat),
         emit_mov_rr(HW_SCRATCH, HW_RAX);
         if (ptr64()) hw_sys_arg_reload(e.bpc + e.broff);
         emit_test_rr(HW_SCRATCH, HW_SCRATCH);
         hwcode_t pos = emit_jcc_rel32(JCC_JNZ);
         rel32_branch(e.bpc + e.broff, pos));
}

SYS(branch_on_bad)
// ----------------------------------------------------------------------------
//   Branch if input stream is bad
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_stream_bad,
         IMM(0, e.knat),
         emit_mov_rr(HW_SCRATCH, HW_RAX);
         if (ptr64()) hw_sys_arg_reload(e.bpc + e.broff);
         emit_test_rr(HW_SCRATCH, HW_SCRATCH);
         hwcode_t pos = emit_jcc_rel32(JCC_JNZ);
         rel32_branch(e.bpc + e.broff, pos));
}

SYS(read_char)
// ----------------------------------------------------------------------------
//   Read character from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_char, IMM(0, e.knat), RET(e.dst));
}

SYS(read_integer)
// ----------------------------------------------------------------------------
//   Read integer from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_integer, IMM(0, e.knat), RET(e.dst));
}

SYS(read_natural)
// ----------------------------------------------------------------------------
//   Read natural number from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_natural, IMM(0, e.knat), RET(e.dst));
}

SYS(read_float)
// ----------------------------------------------------------------------------
//   Read float from input stream
// ----------------------------------------------------------------------------
{
    if (ptr64())
        CALL(xlr8_jit_read_float, IMM(0, e.knat),
             x86reg_t d = hw(local(e.dst, RSPEC_FLT, false));
             emit_sse_scalar_op(0xf3, 0x10, d, HW_XMM0F));
    else
        CALL(xlr8_jit_read_float, IMM(0, e.knat), FRET(e.dst));
}

SYS(read_double)
// ----------------------------------------------------------------------------
//   Read double from input stream
// ----------------------------------------------------------------------------
{
    if (ptr64())
        CALL(xlr8_jit_read_double, IMM(0, e.knat),
             x86reg_t d = hw(local(e.dst, RSPEC_DBL, false));
             emit_sse_scalar_op(0xf2, 0x10, d, HW_XMM0D));
    else
        CALL(xlr8_jit_read_double, IMM(0, e.knat), DRET(e.dst));
}

SYS(write_char)
// ----------------------------------------------------------------------------
//   Write character to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_char, IMM(0, e.knat) ARG(1, e.src), NORET);
}

SYS(write_integer)
// ----------------------------------------------------------------------------
//   Write integer to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_integer, IMM(0, e.knat) ARG(1, e.src), NORET);
}

SYS(write_natural)
// ----------------------------------------------------------------------------
//   Write natural number to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_natural, IMM(0, e.knat) ARG(1, e.src), NORET);
}

SYS(write_float)
// ----------------------------------------------------------------------------
//   Write float to output stream
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t s = load_fp(e.src, 32);
        CALL(xlr8_jit_write_float,
             IMM(0, e.knat);
             if (s != HW_XMM0F) emit_sse_scalar_op(0xf3, 0x10, HW_XMM0F, s),
             NORET);
    }
    else
    {
        CALL(xlr8_jit_write_float, IMM(0, e.knat) FARG(1, e.src), NORET);
    }
}

SYS(write_double)
// ----------------------------------------------------------------------------
//   Write double to output stream
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t s = load_fp(e.src, 64);
        CALL(xlr8_jit_write_double,
             IMM(0, e.knat);
             if (s != HW_XMM0D) emit_sse_scalar_op(0xf2, 0x10, HW_XMM0D, s),
             NORET);
    }
    else
    {
        CALL(xlr8_jit_write_double, IMM(0, e.knat) DARG(1, e.src), NORET);
    }
}

SYS(write_text)
// ----------------------------------------------------------------------------
//   Write text symbol to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_text, IMM(0, e.knat) ARG(1, e.src), NORET);
}

SYS(write_token)
// ----------------------------------------------------------------------------
//   Write token text to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_token, IMM(0, e.knat) IMM(1, e.tok), NORET);
}

SYS(write_newline)
// ----------------------------------------------------------------------------
//   Write newline to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_newline, IMM(0, e.knat), NORET);
}



SYS(read_scanner)
// ----------------------------------------------------------------------------
//   Scan next token from input stream
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        CALL(xlr8_jit_read_scanner,
             emit_sub_rsp_imm(16); IMM(0, e.knat);
             emit_lea(hw_sysv_arg(1), HW_RSP, 0);
             emit_lea(hw_sysv_arg(2), HW_RSP, 8),
             emit_load_u64(hw_dst_local(e.dst), HW_RSP, 0);
             emit_load_u64(hw_dst_local(local_t(e.dst + 1)), HW_RSP, 8);
             emit_add_rsp_imm(16));
    }
    else
    {
        hw_sys_arg_respill();
        emit_sub_rsp_imm(16);
        emit_lea(HW_RAX, HW_RSP, 8);
        emit_push_reg(HW_RAX);
        emit_lea(HW_RAX, HW_RSP, 4);
        emit_push_reg(HW_RAX);
        set_imm(HW_RAX, uint64_t(e.knat));
        emit_push_reg(HW_RAX);
        syscall(reinterpret_cast<void (*)()>(xlr8_jit_read_scanner));
        emit_add_esp_imm(12);
        emit_load_u64(hw_dst_local(e.dst), HW_RSP, 0);
        emit_load_u64(hw_dst_local(local_t(e.dst + 1)), HW_RSP, 8);
        emit_add_rsp_imm(16);
    }
}


SYS(nat)
// ----------------------------------------------------------------------------
//   Append natural number to scanner output
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_nat_append, IMM(0, e.knat) ARG(1, e.src), NORET);
}


SYS(int)
// ----------------------------------------------------------------------------
//   Append integer to scanner output
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_int_append, IMM(0, e.knat) ARG(1, e.src), NORET);
}


SYS(flt)
// ----------------------------------------------------------------------------
//   Append float to scanner output
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t s = load_fp(e.src, 32);
        CALL(xlr8_jit_flt_append,
             IMM(0, e.knat);
             if (s != HW_XMM0F) emit_sse_scalar_op(0xf3, 0x10, HW_XMM0F, s),
             NORET);
    }
    else
    {
        CALL(xlr8_jit_flt_append, IMM(0, e.knat) FARG(1, e.src), NORET);
    }
}


SYS(dbl)
// ----------------------------------------------------------------------------
//   Append double to scanner output
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t s = load_fp(e.src, 64);
        CALL(xlr8_jit_dbl_append,
             IMM(0, e.knat);
             if (s != HW_XMM0D) emit_sse_scalar_op(0xf2, 0x10, HW_XMM0D, s),
             NORET);
    }
    else
    {
        CALL(xlr8_jit_dbl_append, IMM(0, e.knat) DARG(1, e.src), NORET);
    }
}



SYS(print_error)
// ----------------------------------------------------------------------------
//   Print runtime error to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_print_error, IMM(0, e.knat), NORET);
}


SYS(clear_error)
// ----------------------------------------------------------------------------
//   Clear runtime error state
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_clear_error, /* no input */, NORET);
}


SYS(trap_error)
// ----------------------------------------------------------------------------
//   Branch if a runtime error is set
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_has_error,
         /* no input */,
         emit_mov_rr(HW_SCRATCH, HW_RAX);
         if (ptr64()) hw_sys_arg_reload(e.bpc + e.broff);
         emit_test_rr(HW_SCRATCH, HW_SCRATCH);
         hwcode_t pos = emit_jcc_rel32(JCC_JNZ);
         rel32_branch(e.bpc + e.broff, pos));
}



SYS(i2f)
// ----------------------------------------------------------------------------
//   Convert integer to float
// ----------------------------------------------------------------------------
{
    x86reg_t s = hw_local(e.src);
    x86reg_t d = hw(local(e.dst, RSPEC_FLT, false));
    emit8(0xf3);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2a);
    emit8(modrm_rr(d, s));
}


SYS(i2d)
// ----------------------------------------------------------------------------
//   Convert integer to double
// ----------------------------------------------------------------------------
{
    x86reg_t s = hw_local(e.src);
    x86reg_t d = hw(local(e.dst, RSPEC_DBL, false));
    emit8(0xf2);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2a);
    emit8(modrm_rr(d, s));
}


SYS(u2f)
// ----------------------------------------------------------------------------
//   Convert natural to float
// ----------------------------------------------------------------------------
{
    x86reg_t s = hw_local(e.src);
    x86reg_t d = hw(local(e.dst, RSPEC_FLT, false));
    emit8(0xf3);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2a);
    emit8(modrm_rr(d, s));
}


SYS(u2d)
// ----------------------------------------------------------------------------
//   Convert natural to double
// ----------------------------------------------------------------------------
{
    x86reg_t s = hw_local(e.src);
    x86reg_t d = hw(local(e.dst, RSPEC_DBL, false));
    emit8(0xf2);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2a);
    emit8(modrm_rr(d, s));
}


SYS(f2i)
// ----------------------------------------------------------------------------
//   Convert float to integer
// ----------------------------------------------------------------------------
{
    x86reg_t s = load_fp(e.src, 32);
    x86reg_t d = hw_dst_local(e.dst);
    emit8(0xf3);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2c);
    emit8(modrm_rr(d, s));
}


SYS(d2i)
// ----------------------------------------------------------------------------
//   Convert double to integer
// ----------------------------------------------------------------------------
{
    x86reg_t s = load_fp(e.src, 64);
    x86reg_t d = hw_dst_local(e.dst);
    emit8(0xf2);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2c);
    emit8(modrm_rr(d, s));
}


SYS(f2u)
// ----------------------------------------------------------------------------
//   Convert float to natural
// ----------------------------------------------------------------------------
{
    x86reg_t s = load_fp(e.src, 32);
    x86reg_t d = hw_dst_local(e.dst);
    emit8(0xf3);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2c);
    emit8(modrm_rr(d, s));
}


SYS(d2u)
// ----------------------------------------------------------------------------
//   Convert double to natural
// ----------------------------------------------------------------------------
{
    x86reg_t s = load_fp(e.src, 64);
    x86reg_t d = hw_dst_local(e.dst);
    emit8(0xf2);
    if (ptr64())
        emit8(byte(0x48 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    else if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x2c);
    emit8(modrm_rr(d, s));
}


SYS(f2d)
// ----------------------------------------------------------------------------
//   Convert float to double
// ----------------------------------------------------------------------------
{
    x86reg_t s = load_fp(e.src, 32);
    x86reg_t d = hw(local(e.dst, RSPEC_DBL, false));
    // cvtss2sd xmm, xmm: F3 0F 5A /r
    emit8(0xf3);
    if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x5a);
    emit8(modrm_rr(d, s));
}


SYS(d2f)
// ----------------------------------------------------------------------------
//   Convert double to float
// ----------------------------------------------------------------------------
{
    x86reg_t s = load_fp(e.src, 64);
    x86reg_t d = hw(local(e.dst, RSPEC_FLT, false));
    // cvtsd2ss xmm, xmm: F2 0F 5A /r
    emit8(0xf2);
    if (rex_r(d) || rex_b(s))
        emit8(byte(0x40 | (rex_r(d) ? 4 : 0) | (rex_b(s) ? 1 : 0)));
    emit8(0x0f); emit8(0x5a);
    emit8(modrm_rr(d, s));
}


SYS(fpush)
// ----------------------------------------------------------------------------
//   Push float bits onto runtime stack
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t s = load_fp(e.src, 32);
        CALL(xlr8_jit_fpush,
             if (s != HW_XMM0F) emit_sse_scalar_op(0xf3, 0x10, HW_XMM0F, s),
             NORET);
    }
    else
    {
        CALL(xlr8_jit_fpush, FARG(0, e.src), NORET);
    }
}


SYS(dpush)
// ----------------------------------------------------------------------------
//   Push double bits onto runtime stack
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t s = load_fp(e.src, 64);
        CALL(xlr8_jit_dpush,
             if (s != HW_XMM0D) emit_sse_scalar_op(0xf2, 0x10, HW_XMM0D, s),
             NORET);
    }
    else
    {
        CALL(xlr8_jit_dpush, DARG(0, e.src), NORET);
    }
}


SYS(fpop)
// ----------------------------------------------------------------------------
//   Pop float bits from runtime stack
// ----------------------------------------------------------------------------
{
    if (ptr64())
        CALL(xlr8_jit_fpop,
             /* no input */,
             x86reg_t d = hw(local(e.dst, RSPEC_FLT, false));
             if (d != HW_XMM0F) emit_sse_scalar_op(0xf3, 0x10, d, HW_XMM0F));
    else
        CALL(xlr8_jit_fpop, /* no input */, FRET(e.dst));
}


SYS(dpop)
// ----------------------------------------------------------------------------
//   Pop double bits from runtime stack
// ----------------------------------------------------------------------------
{
    if (ptr64())
        CALL(xlr8_jit_dpop,
             /* no input */,
             x86reg_t d = hw(local(e.dst, RSPEC_DBL, false));
             if (d != HW_XMM0D) emit_sse_scalar_op(0xf2, 0x10, d, HW_XMM0D));
    else
        CALL(xlr8_jit_dpop, /* no input */, DRET(e.dst));
}


SYS(fmod)
// ----------------------------------------------------------------------------
//   Float remainder (modulo)
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t d = load_fp(e.dst, 32);
        x86reg_t s = load_fp(e.src, 32);
        CALL(static_cast<float (*)(float, float)>(::fmodf),
             if (d != HW_XMM0F) emit_sse_scalar_op(0xf3, 0x10, HW_XMM0F, d);
             if (s != HW_XMM1F) emit_sse_scalar_op(0xf3, 0x10, HW_XMM1F, s),
             if (d != HW_XMM0F) emit_sse_scalar_op(0xf3, 0x10, d, HW_XMM0F));
    }
    else
    {
        CALL(static_cast<float (*)(float, float)>(::fmodf),
             FARG(0, e.dst) FARG(1, e.src), FRET(e.dst));
    }
}


SYS(dmod)
// ----------------------------------------------------------------------------
//   Double remainder (modulo)
// ----------------------------------------------------------------------------
{
    if (ptr64())
    {
        x86reg_t d = load_fp(e.dst, 64);
        x86reg_t s = load_fp(e.src, 64);
        CALL(static_cast<double (*)(double, double)>(::fmod),
             if (d != HW_XMM0D) emit_sse_scalar_op(0xf2, 0x10, HW_XMM0D, d);
             if (s != HW_XMM1D) emit_sse_scalar_op(0xf2, 0x10, HW_XMM1D, s),
             if (d != HW_XMM0D) emit_sse_scalar_op(0xf2, 0x10, d, HW_XMM0D));
    }
    else
    {
        // Doubles occupy two dword slots each under cdecl
        CALL(static_cast<double (*)(double, double)>(::fmod),
             DARG(0, e.dst) DARG(2, e.src), DRET(e.dst));
    }
}


#define REAL_OP(op, ffn, dfn)                                    \
SYS(f##op)                                                      \
{                                                               \
    if (ptr64())                                                \
    {                                                           \
        x86reg_t s = load_fp(e.src, 32);                        \
        x86reg_t d = hw(local(e.dst, RSPEC_FLT, false));        \
        CALL(static_cast<float (*)(float)>(ffn),                \
             if (s != HW_XMM0F)                                \
                 emit_sse_scalar_op(0xf3, 0x10, HW_XMM0F, s),  \
             if (d != HW_XMM0F)                                \
                 emit_sse_scalar_op(0xf3, 0x10, d, HW_XMM0F)); \
    }                                                           \
    else                                                        \
    {                                                           \
        CALL(static_cast<float (*)(float)>(ffn),                \
             FARG(0, e.src), FRET(e.dst));                      \
    }                                                           \
}                                                               \
                                                                \
SYS(d##op)                                                      \
{                                                               \
    if (ptr64())                                                \
    {                                                           \
        x86reg_t s = load_fp(e.src, 64);                        \
        x86reg_t d = hw(local(e.dst, RSPEC_DBL, false));        \
        CALL(static_cast<double (*)(double)>(dfn),              \
             if (s != HW_XMM0D)                                \
                 emit_sse_scalar_op(0xf2, 0x10, HW_XMM0D, s),  \
             if (d != HW_XMM0D)                                \
                 emit_sse_scalar_op(0xf2, 0x10, d, HW_XMM0D)); \
    }                                                           \
    else                                                        \
    {                                                           \
        CALL(static_cast<double (*)(double)>(dfn),              \
             DARG(0, e.src), DRET(e.dst));                      \
    }                                                           \
}


REAL_OP(floor, ::floorf, ::floor)
REAL_OP(ceil,  ::ceilf,  ::ceil)
REAL_OP(sqrt,  ::sqrtf,  ::sqrt)
REAL_OP(cbrt,  ::cbrtf,  ::cbrt)
REAL_OP(sin,   ::sinf,   ::sin)
REAL_OP(cos,   ::cosf,   ::cos)
REAL_OP(tan,   ::tanf,   ::tan)
REAL_OP(asin,  ::asinf,  ::asin)
REAL_OP(acos,  ::acosf,  ::acos)
REAL_OP(atan,  ::atanf,  ::atan)
REAL_OP(exp,   ::expf,   ::exp)
REAL_OP(log,   ::logf,   ::log)
REAL_OP(exp2,  ::exp2f,  ::exp2)
REAL_OP(log2,  ::log2f,  ::log2)


#define REAL_CMP(op, jcc)                                               \
SYS(f##op)                                                              \
{                                                                       \
    x86reg_t d = load_fp(e.dst, 32);                                    \
    x86reg_t s = load_fp(e.src, 32);                                    \
    emit_pred_fp_cmp(e.pred, d, s, jcc, false);                         \
}                                                                       \
                                                                        \
SYS(d##op)                                                              \
{                                                                       \
    x86reg_t d = load_fp(e.dst, 64);                                    \
    x86reg_t s = load_fp(e.src, 64);                                    \
    emit_pred_fp_cmp(e.pred, d, s, jcc, true);                          \
}

REAL_CMP(cmpeq, SETCC_EQ)
REAL_CMP(cmpne, SETCC_NE)
REAL_CMP(cmplt, 0x92)
REAL_CMP(cmpgt, 0x97)
REAL_CMP(cmple, 0x96)
REAL_CMP(cmpge, 0x93)


SYS(stack)
// ----------------------------------------------------------------------------
//   Get runtime stack pointer
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_stack_ptr, /* no input */, RET(e.dst));
}



SYS(st8v) { store_mem(e.dst, e.src, 8); }
SYS(st4v) { store_mem(e.dst, e.src, 4); }
SYS(st2v) { store_mem(e.dst, e.src, 2); }
SYS(st1v) { store_mem(e.dst, e.src, 1); }

SYS(ld8v) { load_mem(e.dst, e.src, 8); }
SYS(ld4v) { load_mem(e.dst, e.src, 4); }
SYS(ld2v) { load_mem(e.dst, e.src, 2); }
SYS(ld1v) { load_mem(e.dst, e.src, 1); }

SYS(ld8vs) { load_sext(e.dst, e.src, 64); }
SYS(ld4vs) { load_sext(e.dst, e.src, 32); }
SYS(ld2vs) { load_sext(e.dst, e.src, 16); }
SYS(ld1vs) { load_sext(e.dst, e.src, 8); }



SYS(tok)
// ----------------------------------------------------------------------------
//   Get address of token by index
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_tokaddr, ARG(0, e.src), RET(e.dst));
}


SYS(leb2i)
// ----------------------------------------------------------------------------
//   Decode LEB128 signed integer
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_leb2i,
         ARG(0, e.src),
         emit_mov_rr(hw_dst_local(e.dst), HW_RAX);
         emit_mov_rr(hw_dst_local(e.src), HW_RDX));
}


SYS(leb2u)
// ----------------------------------------------------------------------------
//   Decode LEB128 unsigned integer
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_leb2u,
         ARG(0, e.src),
         emit_mov_rr(hw_dst_local(e.dst), HW_RAX);
         emit_mov_rr(hw_dst_local(e.src), HW_RDX));
}


SYS(i2leb)
// ----------------------------------------------------------------------------
//   Encode signed integer as LEB128
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_i2leb, ARG(0, e.dst) ARG(1, e.src), RET(e.dst));
}


SYS(u2leb)
// ----------------------------------------------------------------------------
//   Encode unsigned integer as LEB128
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_u2leb, ARG(0, e.dst) ARG(1, e.src), RET(e.dst));
}


SYS(i2lebsz)
// ----------------------------------------------------------------------------
//   Size of signed integer as LEB128
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_i2lebsz, ARG(0, e.src), RET(e.dst));
}


SYS(u2lebsz)
// ----------------------------------------------------------------------------
//   Size of unsigned integer as LEB128
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_u2lebsz, ARG(0, e.src), RET(e.dst));
}


SYS(cstring)
// ----------------------------------------------------------------------------
//   Get C string for token
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_cstring, IMM(0, e.tok), RET(e.dst));
}
