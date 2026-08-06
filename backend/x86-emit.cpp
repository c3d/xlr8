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
    // Epilogue is emitted by finish_translation()
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
    if (ptr64())
        hw_sys_arg_reload();
    else
        spill_all();
    hwcode_t site = emit_near_jmp_rel32();
    rel32_function(e.tok, site);
    record(x86_ops, "emit_jump %llu", e.tok);
}


EMIT(call)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_call %llu", e.tok);
    if (ptr64())
        hw_sys_arg_reload();
    else
        spill_all();
    emit8(0xE8);
    hwcode_t site = cursor();
    emit32le(0);
    rel32_function(e.tok, site);
}


EMIT(ijmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(e.src);
    record(x86_ops, "emit_ijmp %u reg %u", e.src, r);
    if (ptr64())
        hw_sys_arg_reload();
    else
        spill_all();
    emit_jmp_rr(r);
}


EMIT(icall)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(e.src);
    record(x86_ops, "emit_icall %u reg %u", e.src, r);
    if (ptr64())
        hw_sys_arg_reload();
    else
        spill_all();
    emit_call_rr(r);
}


EMIT(ret)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ret opcode
// ----------------------------------------------------------------------------
{
    if (ptr64())
        hw_sys_arg_reload();
    else
        spill_all();
    epilogue();
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
    frame_size(e.dst);
    if (!ptr64())
    {
        // Emit space for high 32-bits of flags
        emit_xor_rr(HW_RAX, HW_RAX);
        emit_push_reg(HW_RAX);
    }
    emit_push_reg(HW_FRAME);
    emit_mov_rr(HW_FRAME, HW_RSP);
    // Keep SP 16-byte aligned on x86-64.
    int32_t bytes = int32_t(e.dst * sizeof(ularge_t));
    if (ptr64())
        bytes = (bytes + 15) & ~15;
    emit_sub_rsp_imm(bytes);
}


EMIT(leave)
// ----------------------------------------------------------------------------
//   Emit bytecode for the leave opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_leave");
    if (ptr64())
        hw_sys_arg_reload();
    emit_mov_rr(HW_RSP, HW_FRAME);
    emit_pop_reg(HW_FRAME);
    if (!ptr64())
        emit_pop_reg(HW_RAX);
    forget_registers();
}


EMIT(get)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    x86reg_t dr  = hw_local(e.dst);
    int      off = arg_disp(e.src);
    record(x86_ops, "emit_get [%u] = %u reg %u offs %d",
           e.dst, e.src, dr, off);
    emit_load_u64(dr, HW_FRAME, off);
}


EMIT(set)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    x86reg_t sr  = hw_local(e.src);
    int      off = arg_disp(e.dst);
    record(x86_ops, "emit_set %u = [%u] reg %u offs %d",
           e.dst, e.src, sr, off);
    emit_store_u64(HW_FRAME, sr, off);
}


EMIT(push)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    x86reg_t sr  = hw_local(e.src);
    record(x86_ops, "emit_push %u reg %u", e.src, sr);
    emit_push_reg(sr);
}


EMIT(pop)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    x86reg_t dr  = hw_local(e.dst);
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
    x86reg_t  r     = hw_local(e.src);
    uintptr_t gaddr = uintptr_t(&rt.globals[e.dst]);
    set_imm(HW_RAX, gaddr);
    emit_store_u64(HW_RAX, r);
    record(x86_ops, "store_global g[%u]=%u addr %p reg %u",
           e.dst, e.src, gaddr, r);
}


EMIT(rcl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(e.dst);
    uintptr_t gaddr = uintptr_t(&rt.globals[e.src]);
    set_imm(HW_RAX, gaddr);
    emit_load_u64(r, HW_RAX);
    record(x86_ops, "store_global g[%u]=%u addr %p reg %u",
           e.dst, e.src, gaddr, r);
}


EMIT(dbra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
{
    x86reg_t r = hw_local(e.dst);
    record(x86_ops, "emit_dbra %u, %lld reg %u", e.dst, e.broff, r);
    emit_grp5_ff(r, 0xc8); // Decr
    if (ptr64())
        hw_sys_arg_reload(e.bpc + e.broff);
    hwcode_t pos = emit_jcc_rel32(JCC_JZ);
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
    binop_local(e.dst, e.src, op_fadd);
}


EMIT(fsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_fsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fsub);
}


EMIT(fmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_fmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fmul);
}


EMIT(fdiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_fdiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fdiv);
}


EMIT(dadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dadd);
}


EMIT(dsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dsub);
}


EMIT(dmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_dmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dmul);
}


EMIT(ddiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(x86_ops, "emit_ddiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_ddiv);
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
#define RET(loc)      hw_sys_ret(loc);
#define SRET(loc,sz)  hw_sys_ret(loc, sz);
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
}

SYS(read_double)
// ----------------------------------------------------------------------------
//   Read double from input stream
// ----------------------------------------------------------------------------
{
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
}

SYS(write_double)
// ----------------------------------------------------------------------------
//   Write double to output stream
// ----------------------------------------------------------------------------
{
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
}


SYS(nat)
// ----------------------------------------------------------------------------
//   Append natural number to scanner output
// ----------------------------------------------------------------------------
{
}


SYS(int)
// ----------------------------------------------------------------------------
//   Append integer to scanner output
// ----------------------------------------------------------------------------
{
}


SYS(flt)
// ----------------------------------------------------------------------------
//   Append float to scanner output
// ----------------------------------------------------------------------------
{
}


SYS(dbl)
// ----------------------------------------------------------------------------
//   Append double to scanner output
// ----------------------------------------------------------------------------
{
}



SYS(print_error)
// ----------------------------------------------------------------------------
//   Print runtime error to output stream
// ----------------------------------------------------------------------------
{
}


SYS(clear_error)
// ----------------------------------------------------------------------------
//   Clear runtime error state
// ----------------------------------------------------------------------------
{
}


SYS(trap_error)
// ----------------------------------------------------------------------------
//   Branch if a runtime error is set
// ----------------------------------------------------------------------------
{
}



SYS(i2f)
// ----------------------------------------------------------------------------
//   Convert integer to float
// ----------------------------------------------------------------------------
{
}


SYS(i2d)
// ----------------------------------------------------------------------------
//   Convert integer to double
// ----------------------------------------------------------------------------
{
}


SYS(u2f)
// ----------------------------------------------------------------------------
//   Convert natural to float
// ----------------------------------------------------------------------------
{
}


SYS(u2d)
// ----------------------------------------------------------------------------
//   Convert natural to double
// ----------------------------------------------------------------------------
{
}


SYS(f2i)
// ----------------------------------------------------------------------------
//   Convert float to integer
// ----------------------------------------------------------------------------
{
}


SYS(d2i)
// ----------------------------------------------------------------------------
//   Convert double to integer
// ----------------------------------------------------------------------------
{
}


SYS(f2u)
// ----------------------------------------------------------------------------
//   Convert float to natural
// ----------------------------------------------------------------------------
{
}


SYS(d2u)
// ----------------------------------------------------------------------------
//   Convert double to natural
// ----------------------------------------------------------------------------
{
}


SYS(f2d)
// ----------------------------------------------------------------------------
//   Convert float to double
// ----------------------------------------------------------------------------
{
}


SYS(d2f)
// ----------------------------------------------------------------------------
//   Convert double to float
// ----------------------------------------------------------------------------
{
}


SYS(fpush)
// ----------------------------------------------------------------------------
//   Push float bits onto runtime stack
// ----------------------------------------------------------------------------
{
}


SYS(dpush)
// ----------------------------------------------------------------------------
//   Push double bits onto runtime stack
// ----------------------------------------------------------------------------
{
}


SYS(fpop)
// ----------------------------------------------------------------------------
//   Pop float bits from runtime stack
// ----------------------------------------------------------------------------
{
}


SYS(dpop)
// ----------------------------------------------------------------------------
//   Pop double bits from runtime stack
// ----------------------------------------------------------------------------
{
}


SYS(fmod)
// ----------------------------------------------------------------------------
//   Float remainder (modulo)
// ----------------------------------------------------------------------------
{
}


SYS(dmod)
// ----------------------------------------------------------------------------
//   Double remainder (modulo)
// ----------------------------------------------------------------------------
{
}


#define REAL_OP(op)                             \
SYS(f##op)                                      \
{                                               \
                                                \
}                                               \
                                                \
SYS(d##op)                                      \
{                                               \
                                                \
}


REAL_OP(floor)
REAL_OP(ceil)
REAL_OP(sqrt)
REAL_OP(cbrt)
REAL_OP(sin)
REAL_OP(cos)
REAL_OP(tan)
REAL_OP(asin)
REAL_OP(acos)
REAL_OP(atan)
REAL_OP(exp)
REAL_OP(log)
REAL_OP(exp2)
REAL_OP(log2)


#define REAL_CMP(op, cmp)                       \
SYS(f##op)                                      \
{                                               \
}                                               \
                                                \
SYS(d##op)                                      \
{                                               \
}

REAL_CMP(cmpeq, == )
REAL_CMP(cmpne, != )
REAL_CMP(cmplt, <  )
REAL_CMP(cmpgt, >  )
REAL_CMP(cmple, <= )
REAL_CMP(cmpge, >= )


SYS(stack)
// ----------------------------------------------------------------------------
//   Get runtime stack pointer
// ----------------------------------------------------------------------------
{
}



SYS(st8v)
// ----------------------------------------------------------------------------
//   Volatile store 8-byte value
// ----------------------------------------------------------------------------
{
}


SYS(st4v)
// ----------------------------------------------------------------------------
//   Volatile store 4-byte value
// ----------------------------------------------------------------------------
{
}


SYS(st2v)
// ----------------------------------------------------------------------------
//   Volatile store 2-byte value
// ----------------------------------------------------------------------------
{
}


SYS(st1v)
// ----------------------------------------------------------------------------
//   Volatile store 1-byte value
// ----------------------------------------------------------------------------
{
}


SYS(ld8v)
// ----------------------------------------------------------------------------
//   Volatile load 8-byte unsigned
// ----------------------------------------------------------------------------
{
}


SYS(ld4v)
// ----------------------------------------------------------------------------
//   Volatile load 4-byte unsigned
// ----------------------------------------------------------------------------
{
}


SYS(ld2v)
// ----------------------------------------------------------------------------
//   Volatile load 2-byte unsigned
// ----------------------------------------------------------------------------
{
}


SYS(ld1v)
// ----------------------------------------------------------------------------
//   Volatile load 1-byte unsigned
// ----------------------------------------------------------------------------
{
}


SYS(ld8vs)
// ----------------------------------------------------------------------------
//   Volatile load 8-byte signed
// ----------------------------------------------------------------------------
{
}


SYS(ld4vs)
// ----------------------------------------------------------------------------
//   Volatile load 4-byte signed
// ----------------------------------------------------------------------------
{
}


SYS(ld2vs)
// ----------------------------------------------------------------------------
//   Volatile load 2-byte signed
// ----------------------------------------------------------------------------
{
}


SYS(ld1vs)
// ----------------------------------------------------------------------------
//   Volatile load 1-byte signed
// ----------------------------------------------------------------------------
{
}



SYS(tok)
// ----------------------------------------------------------------------------
//   Get address of token by index
// ----------------------------------------------------------------------------
{
}


SYS(leb2i)
// ----------------------------------------------------------------------------
//   Decode LEB128 signed integer
// ----------------------------------------------------------------------------
{
}


SYS(leb2u)
// ----------------------------------------------------------------------------
//   Decode LEB128 unsigned integer
// ----------------------------------------------------------------------------
{
}


SYS(i2leb)
// ----------------------------------------------------------------------------
//   Encode signed integer as LEB128
// ----------------------------------------------------------------------------
{
}


SYS(u2leb)
// ----------------------------------------------------------------------------
//   Encode unsigned integer as LEB128
// ----------------------------------------------------------------------------
{
}


SYS(i2lebsz)
// ----------------------------------------------------------------------------
//   Size of signed integer as LEB128
// ----------------------------------------------------------------------------
{
}


SYS(u2lebsz)
// ----------------------------------------------------------------------------
//   Size of unsigned integer as LEB128
// ----------------------------------------------------------------------------
{
}


SYS(cstring)
// ----------------------------------------------------------------------------
//   Get C string for token
// ----------------------------------------------------------------------------
{
}
