// ****************************************************************************
//  arm64-emit.cpp                                                 XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Table-generated opcode emitters for AArch64
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
    record(arm64_ops, "emit_null()");
    e.error = "Translating null - Input bytecode is bogus";
    syscall(xlr8_null_opcode);
}


EMIT(user)
// ----------------------------------------------------------------------------
//   Emit a user-defined opcode (128..255, or registered low opcodes)
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_user(%lld, %p)", e.op, &e.pc);
    syscall(xlr8_user_opcode);
}


EMIT(undef)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_undef(%llu)", e.tok);
    e.error = "Translating undef";
    syscall(xlr8_undef_opcode);
}


EMIT(end)
// ----------------------------------------------------------------------------
//   Emit bytecode for the end opcode (stop translating this range)
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_end");
    if (num_functions == 0)
        emit32le(ldp_post(HW_FRAME, HW_LR, HW_SP, 16));
    emit32le(ret_insn());
    e.pc = e.end;
}


EMIT(int)
// ----------------------------------------------------------------------------
//   Emit bytecode for the int opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_int(%u, %lld)", e.dst, e.kint);
    store_imm(e.dst, e.kint);
}


EMIT(nat)
// ----------------------------------------------------------------------------
//   Emit bytecode for the nat opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_nat(%u, %llu)", e.dst, e.knat);
    store_imm(e.dst, value_t(e.knat));
}


EMIT(flt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the flt opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_flt(%u, %g)", e.dst, e.kflt);
    store_float(e.dst, e.kflt);
}


EMIT(dbl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbl opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_dbl(%u, %g)", e.dst, e.kdbl);
    store_double(e.dst, e.kdbl);
}


EMIT(txt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the txt opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_txt(%u, %llu)", e.dst, e.tok);
    store_imm(e.dst, value_t(e.tok));
}


EMIT(copy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the copy opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_copy(%u, %u)", e.dst, e.src);
    mov_local(e.dst, e.src);
}


EMIT(swap)
// ----------------------------------------------------------------------------
//   Emit bytecode for the swap opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_swap(%u, %u)", e.dst, e.src);
    swap_locals(e.dst, e.src);
}


EMIT(ccopy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ccopy opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ccopy %u? %u=%u)", e.pred, e.dst, e.src);
    cond_mov_local(e.pred, e.dst, e.src);
}


EMIT(select)
// ----------------------------------------------------------------------------
//   Emit bytecode for the select opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_select %u = %u ? %u : %u",
           e.dst, e.pred, e.src, e.xtra);
    select_local(e.pred, e.dst, e.src, e.xtra);
}


EMIT(jmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the jmp opcode
// ----------------------------------------------------------------------------
{
    hwcode_t site = emit_b_rel();
    rel_function(e.tok, site);
    record(arm64_ops, "emit_jump %llu", e.tok);
}


EMIT(call)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode (AAPCS64 / host ABI)
// ----------------------------------------------------------------------------
//   Place live locals 0..7 in x0–x7, and preserve local size across call
{
    record(arm64_ops, "emit_call %llu", e.tok);
    if (!local_frame_size)
    {
        record(arm64_warning, "Calling tok %u  without a frame size", e.tok);
        emit32le(stp_pre(HW_FRAME, HW_LR, HW_SP, -16));
    }
    hwcode_t site = emit_bl_rel();
    rel_function(e.tok, site);
    if (!local_frame_size)
        emit32le(ldp_post(HW_FRAME, HW_LR, HW_SP, 16));
}



EMIT(ijmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_local(e.src);
    record(arm64_ops, "emit_ijmp %u reg %u", e.src, r);
    // TODO: Translate bytecode address to physical address
    emit32le(br(r));
}


EMIT(icall)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_local(e.src);
    record(arm64_ops, "emit_icall %u reg %u", e.src, r);
    // TODO: Translate bytecode address to physical address
    if (!local_frame_size)
    {
        record(arm64_warning, "Calling reg %u without a frame size", e.src);
        emit32le(stp_pre(HW_FRAME, HW_LR, HW_SP, -16));
    }
    emit32le(blr(r));
    if (!local_frame_size)
        emit32le(ldp_post(HW_FRAME, HW_LR, HW_SP, 16));
}


EMIT(ret)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ret opcode
// ----------------------------------------------------------------------------
{
    emit32le(ret_insn());
}


EMIT(ppc)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ppc opcode (push the source bytecode pc)
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ppc");
    set_imm(HW_SCRATCH, uint64_t(e.pc));
    emit_push(HW_SCRATCH);
}


EMIT(pc)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pc opcode (load bytecode pc)
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_dst_local(e.dst);
    record(arm64_ops, "emit_pc %u reg %u", e.dst, r);
    set_imm(r, uint64_t(e.pc));
}


EMIT(pca)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pca opcode (PC-relative offset of the bytecode
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_dst_local(e.dst);
    record(arm64_ops, "emit_pca %u reg %u = %lld)", e.dst, e.broff);
    set_imm(r, uint64_t(e.ppc + e.broff));
}


EMIT(enter)
// ----------------------------------------------------------------------------
//   Emit bytecode for the enter opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_enter %u", e.dst);

    uint frame = e.dst;
    if (local_frame_size && num_functions)
        record(arm64_error, "Enter called twice in a function, %u after %u",
               frame, local_frame_size);
    local_frame_size = frame;
    frame_size(frame);
    if (frame > HW_SHADOWS)
        frame = HW_SHADOWS;
    for (uint i = 0; i < frame; i += 2)
    {
        if (i + 1 < frame)
            emit32le(stp_off(i+1, i, HW_FRAME, local_disp(i+1)));
        else
            emit_store_off(HW_FRAME, arm64reg_t(HW_X0+i), local_disp(i));
    }
    emit32le(stp_pre(HW_FRAME, HW_LR, HW_SP, -16));
    emit32le(mov_rr(HW_FRAME, HW_SP));

    // Keep SP 16-byte aligned (AAPCS64).
    uint bytes = uint(e.dst) * uint(sizeof(ularge_t));
    bytes = (bytes + 15u) & ~15u;
    if (bytes <= 0xfffu)
    {
        emit32le(sub_imm(HW_SP, HW_SP, bytes));
    }
    else
    {
        // sub (shifted reg) treats Rn/Rd=31 as XZR; go with sub_ext
        set_imm(HW_SCRATCH, bytes);
        emit32le(sub_ext(HW_SP, HW_SP, HW_SCRATCH));
    }
}


EMIT(leave)
// ----------------------------------------------------------------------------
//   Emit bytecode for the leave opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_leave");
    emit32le(mov_rr(HW_SP, HW_FRAME));
    emit32le(ldp_post(HW_FRAME, HW_LR, HW_SP, 16));

    // Outer frame is restored; recover registers
    uint frame = local_frame_size;
    if (!frame)
        record(arm64_error, "Running leave without having run enter");
    if (frame > HW_SHADOWS)
        frame = HW_SHADOWS;
    for (uint i = 0; i < frame; i += 2)
    {
        if (i + 1 < frame)
            emit32le(ldp_off(i+1, i, HW_FRAME, local_disp(i+1)));
        else
            emit_load_off(arm64reg_t(HW_X0+i), HW_FRAME, local_disp(i));
    }
    local_frame_size = 0;
}


EMIT(get)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t dr  = hw_dst_local(e.dst);
    emit_load_off(HW_SCRATCH, HW_FRAME, 0);
    emit_load_off(dr, HW_SCRATCH, local_disp(e.src));
    record(arm64_ops, "emit_get [%u] = %u reg %u offs %d",
           e.dst, e.src, dr, local_disp(e.src));
}


EMIT(set)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t sr  = hw_local(e.src);
    emit_load_off(HW_SCRATCH, HW_FRAME, 0);
    emit_store_off(HW_SCRATCH, sr, local_disp(e.dst));
    record(arm64_ops, "emit_set %u = [%u] reg %u offs %d",
           e.dst, e.src, sr, local_disp(e.dst));
}


EMIT(push)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t sr  = hw_local(e.src);
    record(arm64_ops, "emit_push %u reg %u", e.src, sr);
    emit_push(sr);
}


EMIT(pop)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t dr  = hw_dst_local(e.dst);
    record(arm64_ops, "emit_pop %u reg %u", e.dst, dr);
    emit_pop(dr);
}


EMIT(sys)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sys opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_sys %llu args=%p", e.tok, e.pc);
    dispatch_sys(e);
}


EMIT(sto)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sto opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t  r     = hw_local(e.src);
    uintptr_t gaddr = uintptr_t(&rt.globals[e.dst]);
    set_imm(HW_SCRATCH, gaddr);
    emit_store_off(HW_SCRATCH, r, 0);
    record(arm64_ops, "store_global g[%u]=%u addr %p reg %u",
           e.dst, e.src, gaddr, r);
}


EMIT(rcl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_dst_local(e.dst);
    uintptr_t gaddr = uintptr_t(&rt.globals[e.src]);
    set_imm(HW_SCRATCH, gaddr);
    emit_load_off(r, HW_SCRATCH, 0);
    record(arm64_ops, "load_global g[%u]=%u addr %p reg %u",
           e.dst, e.src, gaddr, r);
}


EMIT(dbra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
{
    arm64reg_t r = hw_dst_local(e.dst);
    record(arm64_ops, "emit_dbra %u, %lld reg %u", e.dst, e.broff, r);
    emit32le(sub_imm(r, r, 1));
    hwcode_t pos = emit_cbnz_rel(r);
    rel_branch(e.bpc + e.broff, pos);
}


EMIT(bra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bra opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_bra %lld + %lld -> %lld",
           e.bpc, e.broff, e.bpc+e.broff);
    hwcode_t site = emit_b_rel();
    rel_branch(e.bpc + e.broff, site);
    e.pc = e.end;               // Pick up next translation spot
}


EMIT(btrue)
// ----------------------------------------------------------------------------
//   Emit bytecode for the btrue opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_btrue(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.bpc + e.broff, true);
}


EMIT(bfalse)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bfalse opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_bfalse(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.bpc + e.broff, false);
}


EMIT(beq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the beq opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_beq(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_EQ);
}


EMIT(bne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bne opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_bne(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_NE);
}


EMIT(blt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the blt opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_blt(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_LT);
}


EMIT(bgt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bgt opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_bgt(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_GT);
}


EMIT(ble)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ble opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ble(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_LE);
}


EMIT(bge)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bge opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_bge(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_GE);
}


EMIT(switch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the switch opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_switch(%u)", e.src);
    dispatch(e.src, e.pc, COND_EQ);
}


EMIT(dispatch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dispatch opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_dispatch(%u)", e.src);
    dispatch(e.src, e.pc, COND_GE);
}


EMIT(true)
// ----------------------------------------------------------------------------
//   Emit bytecode for the true opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_true(%u)", e.pred);
    set_pred(e.pred, true);
}


EMIT(false)
// ----------------------------------------------------------------------------
//   Emit bytecode for the false opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_false(%u)", e.pred);
    set_pred(e.pred, false);
}


EMIT(cmpeq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeq opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpeq(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_EQ);
}


EMIT(cmpne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpne opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpne(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_NE);
}


EMIT(cmplt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplt opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmplt(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_LT);
}


EMIT(cmple)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmple opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmple(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_LE);
}


EMIT(cmpult)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpult opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpult(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_CC);
}


EMIT(cmpule)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpule opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpule(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_LS);
}


EMIT(cmpeqi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeqi opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpeqi(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_EQ);
}


EMIT(cmpnei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpnei opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpnei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_NE);
}


EMIT(cmplti)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplti opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmplti(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_LT);
}


EMIT(cmplei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplei opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmplei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_LE);
}


EMIT(cmpequ)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpequ opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpequ(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_EQ);
}


EMIT(cmpneu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpneu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpneu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_NE);
}


EMIT(cmpltu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpltu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpltu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_CC);
}


EMIT(cmpleu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpleu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_cmpleu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_LS);
}


EMIT(pushf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pushf opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_pushf()");
    emit_push(HW_FLAGS);
}


EMIT(popf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the popf opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_popf()");
    emit_pop(HW_FLAGS);
}


EMIT(add)
// ----------------------------------------------------------------------------
//   Emit bytecode for the add opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_add(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_add);
}


EMIT(sub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sub opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_sub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_sub);
}


EMIT(mul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mul opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_mul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_mul);
}


EMIT(umul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umul opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_umul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_umul);
}


EMIT(div)
// ----------------------------------------------------------------------------
//   Emit bytecode for the div opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_div(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_div);
}


EMIT(udiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the udiv opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_udiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_udiv);
}


EMIT(mod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mod opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_mod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_mod);
}


EMIT(umod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umod opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_umod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_umod);
}


EMIT(and)
// ----------------------------------------------------------------------------
//   Emit bytecode for the and opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_and(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_and);
}


EMIT(or)
// ----------------------------------------------------------------------------
//   Emit bytecode for the or opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_or(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_or);
}


EMIT(xor)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xor opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_xor(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_xor);
}


EMIT(shl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shl opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_shl(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_shl);
}


EMIT(shr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shr opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_shr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_shr);
}


EMIT(ushr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushr opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ushr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_ushr);
}


EMIT(neg)
// ----------------------------------------------------------------------------
//   Emit bytecode for the neg opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_neg(%u)", e.dst);
    neg_local(e.dst);
}


EMIT(not)
// ----------------------------------------------------------------------------
//   Emit bytecode for the not opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_not(%u)", e.dst);
    not_local(e.dst);
}


EMIT(incr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the incr opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_incr(%u)", e.dst);
    incr_local(e.dst);
}


EMIT(decr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the decr opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_decr(%u)", e.dst);
    decr_local(e.dst);
}


EMIT(addi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addi opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_addi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_addi);
}


EMIT(addu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_addu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_addu);
}


EMIT(subi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subi opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_subi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_subi);
}


EMIT(subu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_subu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_subu);
}


EMIT(muli)
// ----------------------------------------------------------------------------
//   Emit bytecode for the muli opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_muli(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_muli);
}


EMIT(mulu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mulu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_mulu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_mulu);
}


EMIT(divi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divi opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_divi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_divi);
}


EMIT(divu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_divu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_divu);
}


EMIT(modi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modi opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_modi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_modi);
}


EMIT(modu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_modu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_modu);
}


EMIT(andu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the andu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_andu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_andu);
}


EMIT(oru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the oru opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_oru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_oru);
}


EMIT(xoru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xoru opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_xoru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_xoru);
}


EMIT(shlu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shlu opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_shlu(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_shlu);
}


EMIT(shru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shru opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_shru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_shru);
}


EMIT(ushru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushru opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ushru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_ushru);
}


EMIT(frame)
// ----------------------------------------------------------------------------
//   Emit bytecode for the frame opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_frame(%u, %u)", e.dst, e.src);
    arm64reg_t dr = hw_dst_local(e.dst);
    int      offs = local_disp(e.src);
    // lea dr, [x29 + offs]
    if (offs >= 0 && offs <= 0xfff)
        emit32le(add_imm(xn(dr), xn(HW_FRAME), uint(offs)));
    else if (offs < 0 && offs >= -0xfff)
        emit32le(sub_imm(xn(dr), xn(HW_FRAME), uint(-offs)));
    else
    {
        set_imm(dr, uint64_t(int64_t(offs)));
        emit32le(add_rrr(xn(dr), xn(HW_FRAME), xn(dr)));
    }
}


EMIT(st8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st8(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 8);
}


EMIT(st4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st4(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 4);
}


EMIT(st2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st2(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 2);
}


EMIT(st1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st1(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 1);
}


EMIT(ld8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld8(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 8);
}


EMIT(ld4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld4(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 4);
}


EMIT(ld2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld2(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 2);
}


EMIT(ld1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1 opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld1(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 1);
}


EMIT(st8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 8, e.kint);
}


EMIT(st4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 4, e.kint);
}


EMIT(st2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 2, e.kint);
}


EMIT(st1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_st1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 1, e.kint);
}


EMIT(ld8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 8, e.kint);
}


EMIT(ld4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 4, e.kint);
}


EMIT(ld2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 2, e.kint);
}


EMIT(ld1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1o opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ld1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 1, e.kint);
}


EMIT(lds)
// ----------------------------------------------------------------------------
//   Emit bytecode for the lds opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_lds%u %u=[%u]", e.kshift, e.dst, e.src);
    load_sext(e.dst, e.src, e.kshift);
}


EMIT(ldos)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ldos opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ldos%u %u=[%u+%lld]", e.kshift, e.dst, e.src, e.kint);
    load_sext(e.dst, e.src, e.kshift, e.kint);
}


EMIT(sext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sext opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_sext(%u, %u)", e.dst, e.kshift);
    sext_local(e.dst, e.kshift);
}


EMIT(zext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the zext opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_zext(%u, %u)", e.dst, e.kshift);
    zext_local(e.dst, e.kshift);
}


EMIT(fadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fadd opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_fadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fadd, 32);
}


EMIT(fsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_fsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fsub, 32);
}


EMIT(fmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_fmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fmul, 32);
}


EMIT(fdiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_fdiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fdiv, 32);
}


EMIT(dadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_dadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dadd, 64);
}


EMIT(dsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_dsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dsub, 64);
}


EMIT(dmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_dmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dmul, 64);
}


EMIT(ddiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(arm64_ops, "emit_ddiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_ddiv, 64);
}


// ============================================================================
//
//   Sys and native opcodes (native.tbl)
//
// ============================================================================

// Arm64 SYS emitters covering native.tbl
#define SYS(op) void arm64::emit_sys_##op(emit_args &e)
#define ARG(a, loc)   hw_sys_arg(a, loc);
#define IMM(a, val)   hw_sys_imm(a, val);
#define RET(loc)      hw_sys_ret(loc);
#define NORET         /* no output */

#define XI(loc)       hw(local((loc), RSPEC_INT, true))
#define FI(loc)       hw(local((loc), RSPEC_FLT, true))
#define DI(loc)       hw(local((loc), RSPEC_DBL, true))
#define XO(loc)       hw(local((loc), RSPEC_INT, false))
#define FO(loc)       hw(local((loc), RSPEC_FLT, false))
#define DO(loc)       hw(local((loc), RSPEC_DBL, false))
#define MOVX(d, s)    emit32le(mov_rr((d), (s)))

// Cast through uintptr_t so overloaded C++ libc/math symbols resolve
#define CALL(fn, inputs, outputs)                   \
    do                                              \
    {                                               \
        inputs;                                     \
        syscall(reinterpret_cast<void (*)()>(fn));  \
        outputs;                                    \
        hw_sys_arg_reload();                        \
    } while (0)

SYS(globals)
// ----------------------------------------------------------------------------
//   Resize globals table to given length
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_globals, IMM(HW_X0, e.klen), NORET);
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
    CALL(::malloc, IMM(HW_X0, e.klen), RET(e.dst));
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
    int64_t bytes = int64_t(e.kint);
    if (bytes > 0)
        bytes = (bytes + 15) & ~int64_t(15);
    else
        bytes = bytes & ~int64_t(15);

    if (bytes > 0 && uint64_t(bytes) <= 0xfffu)
    {
        emit32le(sub_imm(HW_SP, HW_SP, uint(bytes)));
    }
    else if (bytes < 0 && uint64_t(-bytes) <= 0xfffu)
    {
        emit32le(add_imm(HW_SP, HW_SP, uint(-bytes)));
    }
    else if (bytes != 0)
    {
        set_imm(HW_SCRATCH, uint64_t(bytes));
        emit32le(mov_rr(HW_X24, HW_SP));
        emit32le(sub_rrr(xn(HW_X24), xn(HW_X24), xn(HW_SCRATCH)));
        emit32le(mov_rr(HW_SP, HW_X17));
    }
    MOVX(XO(e.dst), HW_SP);
}

SYS(valloca)
// ----------------------------------------------------------------------------
//   Allocate variable-size stack space, return pointer
// ----------------------------------------------------------------------------
{
    // size = (size > 0 ? size + 15 : size) & ~15
    arm64reg_t a = HW_SCRATCH;
    arm64reg_t b = HW_X26;
    MOVX(a, XI(e.src));
    emit32le(add_imm(xn(b), xn(a), 15));
    emit32le(cmp_r_imm(xn(a), 0));
    emit32le(csel_rrr(xn(a), xn(b), xn(a), COND_GT));
    set_imm(b, ~uint64_t(15));
    emit32le(and_rrr(xn(a), xn(a), xn(b)));
    emit32le(mov_rr(b, HW_SP));
    emit32le(sub_rrr(xn(b), xn(b), xn(a)));
    emit32le(mov_rr(HW_SP, b));
    MOVX(XO(e.dst), HW_SP);
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
    CALL(::memcpy, ARG(0, e.dst) ARG(1, e.src) IMM(HW_X2, e.klen), NORET);
}

SYS(kmemmove)
// ----------------------------------------------------------------------------
//   Move memory region of constant length
// ----------------------------------------------------------------------------
{
    CALL(::memmove, ARG(0, e.dst) ARG(1, e.src) set_imm(HW_X2, e.klen), NORET);
}

SYS(kmemset)
// ----------------------------------------------------------------------------
//   Fill memory with byte, constant length
// ----------------------------------------------------------------------------
{
    CALL(::memset, ARG(0, e.dst) ARG(1, e.src) IMM(HW_X2, e.klen), NORET);
}

SYS(memkset)
// ----------------------------------------------------------------------------
//   Fill memory with constant byte value
// ----------------------------------------------------------------------------
{
    CALL(::memset, ARG(0, e.dst) IMM(HW_X1, uint64_t(e.kint)); ARG(2, e.src), NORET);
}

SYS(kmemkset)
// ----------------------------------------------------------------------------
//   Fill memory with constant byte and length
// ----------------------------------------------------------------------------
{
    CALL(::memset, ARG(0, e.dst) IMM(HW_X1, uint64_t(e.kint)); IMM(HW_X2, e.klen), NORET);
}

SYS(memcmp)
// ----------------------------------------------------------------------------
//   Compare two memory regions
// ----------------------------------------------------------------------------
{
    CALL(::memcmp, ARG(0, e.src) ARG(1, e.xtra) ARG(2, e.len), RET(e.dst));
}

SYS(kmemcmp)
// ----------------------------------------------------------------------------
//   Compare memory regions of constant length
// ----------------------------------------------------------------------------
{
    CALL(::memcmp, ARG(0, e.src) ARG(1, e.xtra) IMM(HW_X2, e.klen), RET(e.dst));
}

SYS(memchr)
// ----------------------------------------------------------------------------
//   Find byte in memory region
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr), ARG(0, e.src) ARG(1, e.ival) ARG(2, e.len), RET(e.dst));
}

SYS(kmemchr)
// ----------------------------------------------------------------------------
//   Find byte in memory of constant length
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr), ARG(0, e.src) ARG(1, e.ival) IMM(HW_X2, e.klen), RET(e.dst));
}

SYS(memkchr)
// ----------------------------------------------------------------------------
//   Find constant byte in memory region
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr), ARG(0, e.src) IMM(HW_X1, uint64_t(e.kint)); ARG(2, e.len), RET(e.dst));
}

SYS(kmemkchr)
// ----------------------------------------------------------------------------
//   Find constant byte in memory of constant length
// ----------------------------------------------------------------------------
{
    CALL(static_cast<void *(*)(void *, int, size_t)>(::memchr), ARG(0, e.src) IMM(HW_X1, uint64_t(e.kint)); IMM(HW_X2, e.klen), RET(e.dst));
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
    CALL(::strnlen, ARG(0, e.src) IMM(HW_X1, e.klen), RET(e.dst));
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
    CALL(::strncpy, ARG(0, e.dst) ARG(1, e.src) IMM(HW_X2, e.klen), RET(e.dst));
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
    CALL(::strncat, ARG(0, e.dst) ARG(1, e.src) IMM(HW_X2, e.klen), RET(e.dst));
}

SYS(strcmp)
// ----------------------------------------------------------------------------
//   Compare two C strings
// ----------------------------------------------------------------------------
{
    CALL(::strcmp, ARG(0, e.src) ARG(1, e.xtra), RET(e.dst));
}

SYS(strncmp)
// ----------------------------------------------------------------------------
//   Compare C strings with length bound
// ----------------------------------------------------------------------------
{
    CALL(::strncmp, ARG(0, e.src) ARG(1, e.xtra) ARG(2, e.len), RET(e.dst));
}

SYS(kstrncmp)
// ----------------------------------------------------------------------------
//   Compare C strings with constant bound
// ----------------------------------------------------------------------------
{
    CALL(::strncmp,
         ARG(0, e.src) ARG(1, e.xtra) IMM(HW_X2, e.klen), RET(e.dst));
}

// glibc/libcpp overload strchr/strrchr/strstr as char*(char*,…) /
// const char*(const char*,…). Pick the non-const overload so CALL gets a
// concrete function pointer (char*(const char*,…) is not a valid overload).
SYS(strchr)
// ----------------------------------------------------------------------------
//   Find character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*) (char *, int)>(::strchr),
         ARG(0, e.src) ARG(1, e.ival), RET(e.dst));
}

SYS(kstrchr)
// ----------------------------------------------------------------------------
//   Find constant character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*) (char *, int)>(::strchr),
         ARG(0, e.src) IMM(HW_X1, uint64_t(e.kint)), RET(e.dst));
}

SYS(strrchr)
// ----------------------------------------------------------------------------
//   Find last character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*) (char *, int)>(::strrchr),
         ARG(0, e.src) ARG(1, e.ival), RET(e.dst));
}

SYS(kstrrchr)
// ----------------------------------------------------------------------------
//   Find last constant character in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*) (char *, int)>(::strrchr),
         ARG(0, e.src) IMM(HW_X1, uint64_t(e.kint)), RET(e.dst));
}

SYS(strstr)
// ----------------------------------------------------------------------------
//   Find substring in C string
// ----------------------------------------------------------------------------
{
    CALL(static_cast<char *(*) (char *, const char *)>(::strstr),
         ARG(0, e.src) ARG(1, e.xtra), RET(e.dst));
}

#if 0 // gated with native.tbl (no strnstr on Debian)
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

#endif

SYS(strcasestr)
// ----------------------------------------------------------------------------
//   Find substring case-insensitively
// ----------------------------------------------------------------------------
{
    // glibc: C++ overloads like strchr; Apple: single C declaration
#if defined(__GLIBC__)
    CALL(static_cast<char *(*) (char *, const char *)>(::strcasestr),
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
    CALL(::strndup, ARG(0, e.src) IMM(HW_X1, e.klen), RET(e.dst));
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
         IMM(HW_X0, e.knat),
         hwcode_t pos = emit_cbnz_rel(HW_X0);
         rel_branch(e.bpc + e.broff, pos));
}

SYS(branch_on_bad)
// ----------------------------------------------------------------------------
//   Branch if input stream is bad
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_stream_bad,
         IMM(HW_X0, e.knat),
         hwcode_t pos = emit_cbnz_rel(HW_X0);
         rel_branch(e.bpc + e.broff, pos));
}

SYS(read_char)
// ----------------------------------------------------------------------------
//   Read character from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_char, IMM(HW_X0, e.knat), RET(e.dst));
}

SYS(read_integer)
// ----------------------------------------------------------------------------
//   Read integer from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_integer, IMM(HW_X0, e.knat), RET(e.dst));
}

SYS(read_natural)
// ----------------------------------------------------------------------------
//   Read natural number from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_natural, IMM(HW_X0, e.knat), RET(e.dst));
}

SYS(read_float)
// ----------------------------------------------------------------------------
//   Read float from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_float,
         IMM(HW_X0, e.knat),
         emit32le(fmov_ss(vn(FO(e.dst)), 0)));
}

SYS(read_double)
// ----------------------------------------------------------------------------
//   Read double from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_double,
         IMM(HW_X0, e.knat),
         emit32le(fmov_dd(vn(DI(e.dst)), 0)));
}

SYS(write_char)
// ----------------------------------------------------------------------------
//   Write character to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_char, IMM(HW_X0, e.knat); ARG(1, e.src), NORET);
}

SYS(write_integer)
// ----------------------------------------------------------------------------
//   Write integer to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_integer, IMM(HW_X0, e.knat); ARG(1, e.src), NORET);
}

SYS(write_natural)
// ----------------------------------------------------------------------------
//   Write natural number to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_natural, IMM(HW_X0, e.knat); ARG(1, e.src), NORET);
}

SYS(write_float)
// ----------------------------------------------------------------------------
//   Write float to output stream
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    CALL(xlr8_jit_write_float,
         IMM(HW_X0, e.knat);
         if (vn(s) != 0)
             emit32le(fmov_ss(0, vn(s))),
         NORET);
}

SYS(write_double)
// ----------------------------------------------------------------------------
//   Write double to output stream
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    CALL(xlr8_jit_write_double,
         IMM(HW_X0, e.knat);
         if (vn(s) != 0)
             emit32le(fmov_dd(0, vn(s))),
         NORET);
}

SYS(write_text)
// ----------------------------------------------------------------------------
//   Write text symbol to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_text,
         IMM(HW_X0, e.knat); ARG(1, e.src), NORET);
}

SYS(write_token)
// ----------------------------------------------------------------------------
//   Write token text to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_token,
         IMM(HW_X0, e.knat); IMM(HW_X1, e.tok), NORET);
}

SYS(write_newline)
// ----------------------------------------------------------------------------
//   Write newline to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_write_newline, IMM(HW_X0, e.knat), NORET);
}

SYS(read_scanner)
// ----------------------------------------------------------------------------
//   Scan next token from input stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_read_scanner,
         emit32le(sub_imm(xn(HW_SP), xn(HW_SP), 16)); IMM(HW_X0, e.knat);
         MOVX(HW_X1, HW_SP);
         emit32le(add_imm(xn(HW_X2), xn(HW_SP), 8)),
         emit_load_off(XO(e.dst), HW_SP, 0);
         emit_load_off(XI(local_t(e.dst + 1)), HW_SP, 8);
         emit32le(add_imm(xn(HW_SP), xn(HW_SP), 16)));
}

SYS(nat)
// ----------------------------------------------------------------------------
//   Append natural number to scanner output
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_nat_append, IMM(HW_X0, e.knat); ARG(1, e.src), NORET);
}

SYS(int)
// ----------------------------------------------------------------------------
//   Append integer to scanner output
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_int_append, IMM(HW_X0, e.knat); ARG(1, e.src), NORET);
}

SYS(flt)
// ----------------------------------------------------------------------------
//   Append float to scanner output
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    CALL(xlr8_jit_flt_append,
         IMM(HW_X0, e.knat); if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         NORET);
}

SYS(dbl)
// ----------------------------------------------------------------------------
//   Append double to scanner output
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    CALL(xlr8_jit_dbl_append,
         IMM(HW_X0, e.knat); if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         NORET);
}

SYS(print_error)
// ----------------------------------------------------------------------------
//   Print runtime error to output stream
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_print_error, IMM(HW_X0, e.knat), NORET);
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
         hwcode_t pos = emit_cbnz_rel(HW_X0);
         rel_branch(e.bpc + e.broff, pos));
}

SYS(i2f)
// ----------------------------------------------------------------------------
//   Convert integer to float
// ----------------------------------------------------------------------------
{
    // Evaluate INT source before FLT dest so aliased locals keep the
    // integer binding long enough for scvtf (dest rebind must not steal first).
    arm64reg_t s = XI(e.src);
    arm64reg_t d = FO(e.dst);
    emit32le(scvtf_s_x(vn(d), xn(s)));
}

SYS(i2d)
// ----------------------------------------------------------------------------
//   Convert integer to double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = XI(e.src);
    arm64reg_t d = DI(e.dst);
    emit32le(scvtf_d_x(vn(d), xn(s)));
}

SYS(u2f)
// ----------------------------------------------------------------------------
//   Convert natural to float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = XI(e.src);
    arm64reg_t d = FO(e.dst);
    emit32le(ucvtf_s_x(vn(d), xn(s)));
}

SYS(u2d)
// ----------------------------------------------------------------------------
//   Convert natural to double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = XI(e.src);
    arm64reg_t d = DI(e.dst);
    emit32le(ucvtf_d_x(vn(d), xn(s)));
}

SYS(f2i)
// ----------------------------------------------------------------------------
//   Convert float to integer
// ----------------------------------------------------------------------------
{
    // FLT source before INT dest — same aliasing concern as i2f.
    arm64reg_t s = FI(e.src);
    arm64reg_t d = XO(e.dst);
    emit32le(fcvtzs_x_s(xn(d), vn(s)));
}

SYS(d2i)
// ----------------------------------------------------------------------------
//   Convert double to integer
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = XO(e.dst);
    emit32le(fcvtzs_x_d(xn(d), vn(s)));
}

SYS(f2u)
// ----------------------------------------------------------------------------
//   Convert float to natural
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = XO(e.dst);
    emit32le(fcvtzu_x_s(xn(d), vn(s)));
}

SYS(d2u)
// ----------------------------------------------------------------------------
//   Convert double to natural
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = XO(e.dst);
    emit32le(fcvtzu_x_d(xn(d), vn(s)));
}

SYS(f2d)
// ----------------------------------------------------------------------------
//   Convert float to double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = DI(e.dst);
    emit32le(fcvt_d_s(vn(d), vn(s)));
}

SYS(d2f)
// ----------------------------------------------------------------------------
//   Convert double to float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = FO(e.dst);
    emit32le(fcvt_s_d(vn(d), vn(s)));
}

SYS(fpush)
// ----------------------------------------------------------------------------
//   Push float bits onto runtime stack
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    CALL(xlr8_jit_fpush,
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         NORET);
}

SYS(dpush)
// ----------------------------------------------------------------------------
//   Push double bits onto runtime stack
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    CALL(xlr8_jit_dpush,
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         NORET);
}

SYS(fpop)
// ----------------------------------------------------------------------------
//   Pop float bits from runtime stack
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_fpop,
         /* no input */,
         emit32le(fmov_ss(vn(FO(e.dst)), 0)));
}

SYS(dpop)
// ----------------------------------------------------------------------------
//   Pop double bits from runtime stack
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_dpop,
         /* no input */,
         emit32le(fmov_dd(vn(DI(e.dst)), 0)));
}

SYS(fmod)
// ----------------------------------------------------------------------------
//   Float remainder (modulo)
// ----------------------------------------------------------------------------
{
    arm64reg_t d = FO(e.dst);
    arm64reg_t s = FI(e.src);
    CALL(static_cast<float (*)(float, float)>(::fmodf),
         emit32le(fmov_ss(0, vn(d)));
         emit32le(fmov_ss(1, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dmod)
// ----------------------------------------------------------------------------
//   Double remainder (modulo)
// ----------------------------------------------------------------------------
{
    arm64reg_t d = DI(e.dst);
    arm64reg_t s = DI(e.src);
    CALL(static_cast<double (*)(double, double)>(::fmod),
         emit32le(fmov_dd(0, vn(d)));
         emit32le(fmov_dd(1, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(ffloor)
// ----------------------------------------------------------------------------
//   Floor of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::floorf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dfloor)
// ----------------------------------------------------------------------------
//   Floor of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::floor),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fceil)
// ----------------------------------------------------------------------------
//   Ceiling of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::ceilf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dceil)
// ----------------------------------------------------------------------------
//   Ceiling of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::ceil),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fsqrt)
// ----------------------------------------------------------------------------
//   Square root of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::sqrtf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dsqrt)
// ----------------------------------------------------------------------------
//   Square root of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::sqrt),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fcbrt)
// ----------------------------------------------------------------------------
//   Cube root of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::cbrtf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dcbrt)
// ----------------------------------------------------------------------------
//   Cube root of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::cbrt),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fsin)
// ----------------------------------------------------------------------------
//   Sine of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::sinf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dsin)
// ----------------------------------------------------------------------------
//   Sine of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::sin),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fcos)
// ----------------------------------------------------------------------------
//   Cosine of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::cosf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dcos)
// ----------------------------------------------------------------------------
//   Cosine of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::cos),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(ftan)
// ----------------------------------------------------------------------------
//   Tangent of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::tanf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dtan)
// ----------------------------------------------------------------------------
//   Tangent of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::tan),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fasin)
// ----------------------------------------------------------------------------
//   Arc sine of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::asinf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dasin)
// ----------------------------------------------------------------------------
//   Arc sine of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::asin),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(facos)
// ----------------------------------------------------------------------------
//   Arc cosine of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::acosf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dacos)
// ----------------------------------------------------------------------------
//   Arc cosine of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::acos),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fatan)
// ----------------------------------------------------------------------------
//   Arc tangent of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::atanf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(datan)
// ----------------------------------------------------------------------------
//   Arc tangent of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::atan),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fexp)
// ----------------------------------------------------------------------------
//   Exponential of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::expf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dexp)
// ----------------------------------------------------------------------------
//   Exponential of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::exp),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(flog)
// ----------------------------------------------------------------------------
//   Natural log of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::logf),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dlog)
// ----------------------------------------------------------------------------
//   Natural log of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::log),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fexp2)
// ----------------------------------------------------------------------------
//   Base-2 exponential of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::exp2f),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dexp2)
// ----------------------------------------------------------------------------
//   Base-2 exponential of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::exp2),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(flog2)
// ----------------------------------------------------------------------------
//   Base-2 log of float
// ----------------------------------------------------------------------------
{
    arm64reg_t s = FI(e.src);
    arm64reg_t d = FO(e.dst);
    CALL(static_cast<float (*)(float)>(::log2f),
         if (vn(s) != 0) emit32le(fmov_ss(0, vn(s))),
         emit32le(fmov_ss(vn(d), 0)));
}

SYS(dlog2)
// ----------------------------------------------------------------------------
//   Base-2 log of double
// ----------------------------------------------------------------------------
{
    arm64reg_t s = DI(e.src);
    arm64reg_t d = DI(e.dst);
    CALL(static_cast<double (*)(double)>(::log2),
         if (vn(s) != 0) emit32le(fmov_dd(0, vn(s))),
         emit32le(fmov_dd(vn(d), 0)));
}

SYS(fcmpeq)
// ----------------------------------------------------------------------------
//   Compare floats for equality
// ----------------------------------------------------------------------------
{
    arm64reg_t d = FO(e.dst);
    arm64reg_t s = FI(e.src);
    emit32le(fcmp_ss(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_EQ);
}

SYS(dcmpeq)
// ----------------------------------------------------------------------------
//   Compare doubles for equality
// ----------------------------------------------------------------------------
{
    arm64reg_t d = DI(e.dst);
    arm64reg_t s = DI(e.src);
    emit32le(fcmp_dd(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_EQ);
}

SYS(fcmpne)
// ----------------------------------------------------------------------------
//   Compare floats for inequality
// ----------------------------------------------------------------------------
{
    arm64reg_t d = FO(e.dst);
    arm64reg_t s = FI(e.src);
    emit32le(fcmp_ss(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_NE);
}

SYS(dcmpne)
// ----------------------------------------------------------------------------
//   Compare doubles for inequality
// ----------------------------------------------------------------------------
{
    arm64reg_t d = DI(e.dst);
    arm64reg_t s = DI(e.src);
    emit32le(fcmp_dd(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_NE);
}

SYS(fcmplt)
// ----------------------------------------------------------------------------
//   Compare floats less-than
// ----------------------------------------------------------------------------
{
    arm64reg_t d = FO(e.dst);
    arm64reg_t s = FI(e.src);
    emit32le(fcmp_ss(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_MI);
}

SYS(dcmplt)
// ----------------------------------------------------------------------------
//   Compare doubles less-than
// ----------------------------------------------------------------------------
{
    arm64reg_t d = DI(e.dst);
    arm64reg_t s = DI(e.src);
    emit32le(fcmp_dd(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_MI);
}

SYS(fcmpgt)
// ----------------------------------------------------------------------------
//   Compare floats greater-than
// ----------------------------------------------------------------------------
{
    arm64reg_t d = FO(e.dst);
    arm64reg_t s = FI(e.src);
    emit32le(fcmp_ss(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_GT);
}

SYS(dcmpgt)
// ----------------------------------------------------------------------------
//   Compare doubles greater-than
// ----------------------------------------------------------------------------
{
    arm64reg_t d = DI(e.dst);
    arm64reg_t s = DI(e.src);
    emit32le(fcmp_dd(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_GT);
}

SYS(fcmple)
// ----------------------------------------------------------------------------
//   Compare floats less-or-equal
// ----------------------------------------------------------------------------
{
    arm64reg_t d = FO(e.dst);
    arm64reg_t s = FI(e.src);
    emit32le(fcmp_ss(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_LS);
}

SYS(dcmple)
// ----------------------------------------------------------------------------
//   Compare doubles less-or-equal
// ----------------------------------------------------------------------------
{
    arm64reg_t d = DI(e.dst);
    arm64reg_t s = DI(e.src);
    emit32le(fcmp_dd(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_LS);
}

SYS(fcmpge)
// ----------------------------------------------------------------------------
//   Compare floats greater-or-equal
// ----------------------------------------------------------------------------
{
    arm64reg_t d = FO(e.dst);
    arm64reg_t s = FI(e.src);
    emit32le(fcmp_ss(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_GE);
}

SYS(dcmpge)
// ----------------------------------------------------------------------------
//   Compare doubles greater-or-equal
// ----------------------------------------------------------------------------
{
    arm64reg_t d = DI(e.dst);
    arm64reg_t s = DI(e.src);
    emit32le(fcmp_dd(vn(d), vn(s)));
    set_pred_cmp(e.pred, COND_GE);
}

SYS(stack)
// ----------------------------------------------------------------------------
//   Get runtime stack pointer
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_stack_ptr, /* no input */, RET(e.dst));
}

SYS(st8v)
// ----------------------------------------------------------------------------
//   Volatile store 8-byte value
// ----------------------------------------------------------------------------
{
    store_mem(e.dst, e.src, 8);
}

SYS(st4v)
// ----------------------------------------------------------------------------
//   Volatile store 4-byte value
// ----------------------------------------------------------------------------
{
    store_mem(e.dst, e.src, 4);
}

SYS(st2v)
// ----------------------------------------------------------------------------
//   Volatile store 2-byte value
// ----------------------------------------------------------------------------
{
    store_mem(e.dst, e.src, 2);
}

SYS(st1v)
// ----------------------------------------------------------------------------
//   Volatile store 1-byte value
// ----------------------------------------------------------------------------
{
    store_mem(e.dst, e.src, 1);
}

SYS(ld8v)
// ----------------------------------------------------------------------------
//   Volatile load 8-byte unsigned
// ----------------------------------------------------------------------------
{
    load_mem(e.dst, e.src, 8);
}

SYS(ld4v)
// ----------------------------------------------------------------------------
//   Volatile load 4-byte unsigned
// ----------------------------------------------------------------------------
{
    load_mem(e.dst, e.src, 4);
}

SYS(ld2v)
// ----------------------------------------------------------------------------
//   Volatile load 2-byte unsigned
// ----------------------------------------------------------------------------
{
    load_mem(e.dst, e.src, 2);
}

SYS(ld1v)
// ----------------------------------------------------------------------------
//   Volatile load 1-byte unsigned
// ----------------------------------------------------------------------------
{
    load_mem(e.dst, e.src, 1);
}

SYS(ld8vs)
// ----------------------------------------------------------------------------
//   Volatile load 8-byte signed
// ----------------------------------------------------------------------------
{
    load_sext(e.dst, e.src, 64);
}

SYS(ld4vs)
// ----------------------------------------------------------------------------
//   Volatile load 4-byte signed
// ----------------------------------------------------------------------------
{
    load_sext(e.dst, e.src, 32);
}

SYS(ld2vs)
// ----------------------------------------------------------------------------
//   Volatile load 2-byte signed
// ----------------------------------------------------------------------------
{
    load_sext(e.dst, e.src, 16);
}

SYS(ld1vs)
// ----------------------------------------------------------------------------
//   Volatile load 1-byte signed
// ----------------------------------------------------------------------------
{
    load_sext(e.dst, e.src, 8);
}

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
         MOVX(XO(e.dst), HW_X0);
         MOVX(XI(e.src), HW_X1));
}

SYS(leb2u)
// ----------------------------------------------------------------------------
//   Decode LEB128 unsigned integer
// ----------------------------------------------------------------------------
{
    CALL(xlr8_jit_leb2u,
         ARG(0, e.src),
         MOVX(XO(e.dst), HW_X0);
         MOVX(XI(e.src), HW_X1));
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
    CALL(xlr8_jit_cstring, IMM(HW_X0, e.tok), RET(e.dst));
}
