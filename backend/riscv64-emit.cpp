// ****************************************************************************
//  riscv64-emit.cpp                                             XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Table-generated opcode emitters for RISC-V 64
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
    record(riscv64_ops, "emit_null()");
    e.error = "Translating null - Input bytecode is bogus";
    syscall(xlr8_null_opcode);
}


EMIT(user)
// ----------------------------------------------------------------------------
//   Emit a user-defined opcode (128..255, or registered low opcodes)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_user(%lld, %p)", e.op, &e.pc);
    syscall(xlr8_user_opcode);
}


EMIT(undef)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_undef(%llu)", e.tok);
    e.error = "Translating undef";
    syscall(xlr8_undef_opcode);
}


EMIT(end)
// ----------------------------------------------------------------------------
//   Emit bytecode for the end opcode (stop translating this range)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_end");
    e.pc = e.end;
}


EMIT(int)
// ----------------------------------------------------------------------------
//   Emit bytecode for the int opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_int(%u, %lld)", e.dst, e.kint);
    store_imm(e.dst, e.kint);
}


EMIT(nat)
// ----------------------------------------------------------------------------
//   Emit bytecode for the nat opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_nat(%u, %llu)", e.dst, e.knat);
    store_imm(e.dst, value_t(e.knat));
}


EMIT(flt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the flt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_flt(%u, %g)", e.dst, e.kflt);
    store_float(e.dst, e.kflt);
}


EMIT(dbl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dbl(%u, %g)", e.dst, e.kdbl);
    store_double(e.dst, e.kdbl);
}


EMIT(txt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the txt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_txt(%u, %llu)", e.dst, e.tok);
    store_imm(e.dst, value_t(e.tok));
}


EMIT(copy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the copy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_copy(%u, %u)", e.dst, e.src);
    mov_local(e.dst, e.src);
}


EMIT(swap)
// ----------------------------------------------------------------------------
//   Emit bytecode for the swap opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_swap(%u, %u)", e.dst, e.src);
    swap_locals(e.dst, e.src);
}


EMIT(ccopy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ccopy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ccopy %u? %u=%u)", e.pred, e.dst, e.src);
    cond_mov_local(e.pred, e.dst, e.src);
}


EMIT(select)
// ----------------------------------------------------------------------------
//   Emit bytecode for the select opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_select %u = %u ? %u : %u",
           e.dst, e.pred, e.src, e.xtra);
    select_local(e.pred, e.dst, e.src, e.xtra);
}


EMIT(jmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the jmp opcode
// ----------------------------------------------------------------------------
{
    spill_all();
    hwcode_t site = cursor();
    emit32le(j_imm(0));
    rel_function(e.tok, site);
    record(riscv64_ops, "emit_jmp %llu", e.tok);
}


EMIT(call)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode
// ----------------------------------------------------------------------------
{
    spill_all();
    // Save ra and frame pointer (16-byte aligned)
    emit32le(addi(xn(HW_SP), xn(HW_SP), -16));
    emit32le(sd_off(xn(HW_RA),    xn(HW_SP), 8));
    emit32le(sd_off(xn(HW_FRAME), xn(HW_SP), 0));
    hwcode_t site = cursor();
    emit32le(enc_j_imm(0, xn(HW_RA)));
    rel_function(e.tok, site);
    emit32le(ld_off(xn(HW_FRAME), xn(HW_SP), 0));
    emit32le(ld_off(xn(HW_RA),    xn(HW_SP), 8));
    emit32le(addi(xn(HW_SP), xn(HW_SP), 16));
    record(riscv64_ops, "emit_call %llu", e.tok);
}


EMIT(ijmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(e.src);
    record(riscv64_ops, "emit_ijmp %u reg %u", e.src, r);
    spill_all();
    emit32le(jalr(xn(r)));
}


EMIT(icall)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(e.src);
    record(riscv64_ops, "emit_icall %u reg %u", e.src, r);
    spill_all();
    emit32le(jalr_ra(xn(r)));
}


EMIT(ret)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ret opcode
// ----------------------------------------------------------------------------
{
    spill_all();
    epilogue();
}


EMIT(pca)
// ----------------------------------------------------------------------------
//   Emit PC-relative code address; queue target for JIT translation
// ----------------------------------------------------------------------------
{
    byte_p       target = e.bpc + e.broff;
    riscv64reg_t r      = hw_local(e.dst);
    record(riscv64_ops, "emit_pca %u = %p", e.dst, target);
    branch(target, hwcode_t(-1));
    load_ptr_imm(r, uint64_t(uintptr_t(target)));
}


EMIT(pcd)
// ----------------------------------------------------------------------------
//   Emit PC-relative data address; do not translate the target as code
// ----------------------------------------------------------------------------
{
    byte_p       target = e.bpc + e.broff;
    riscv64reg_t r      = hw_local(e.dst);
    record(riscv64_ops, "emit_pcd %u = %p", e.dst, target);
    load_ptr_imm(r, uint64_t(uintptr_t(target)));
}


EMIT(enter)
// ----------------------------------------------------------------------------
//   Emit bytecode for the enter opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_enter %u", e.dst);
    frame_size(e.dst);
    // Save ra and old frame pointer (2 ABI words)
    int link = 2 * int(pointer_size());
    emit32le(addi(xn(HW_SP), xn(HW_SP), -link));
    emit_store_abi_word(HW_RA,    HW_SP, pointer_size());
    emit_store_abi_word(HW_FRAME, HW_SP, 0);
    emit32le(mv_rr(xn(HW_FRAME), xn(HW_SP)));
    // Allocate locals; keep SP 16-byte aligned.
    uint bytes = uint(e.dst) * uint(sizeof(ularge_t));
    bytes = (bytes + 15u) & ~15u;
    if (bytes > 0 && bytes <= 2047u)
        emit32le(addi(xn(HW_SP), xn(HW_SP), -int(bytes)));
    else if (bytes > 2047u)
    {
        load_imm64(HW_IP, uint64_t(bytes));
        emit32le(sub_rr(xn(HW_SP), xn(HW_SP), xn(HW_IP)));
    }
}


EMIT(leave)
// ----------------------------------------------------------------------------
//   Emit bytecode for the leave opcode
// ----------------------------------------------------------------------------
//   Restores FP/RA and SP (drops locals/alloca).  If the next opcode is ret,
//   fold into epilogue.  Branches that target the ret bytecode are
//   translated separately when that label is picked up.
{
    record(riscv64_ops, "emit_leave");
    int link = 2 * int(pointer_size());
    emit32le(mv_rr(xn(HW_SP), xn(HW_FRAME)));
    emit_load_abi_word(HW_FRAME, HW_SP, 0);
    emit_load_abi_word(HW_RA,    HW_SP, pointer_size());
    emit32le(addi(xn(HW_SP), xn(HW_SP), link));
    forget_registers();
    if (e.pc < e.end && op_t(*e.pc) == op_ret)
    {
        e.pc++;
        record(riscv64_ops, "emit_leave+ret -> epilogue");
        epilogue();
    }
}


EMIT(get)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t dr  = hw_local(e.dst);
    int        off = arg_disp(e.src);
    record(riscv64_ops, "emit_get [%u] = %u reg %u offs %d",
           e.dst, e.src, dr, off);
    emit_load_off(dr, HW_FRAME, off);
}


EMIT(set)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t sr  = hw_local(e.src);
    int        off = arg_disp(e.dst);
    record(riscv64_ops, "emit_set %u = [%u] reg %u offs %d",
           e.dst, e.src, sr, off);
    emit_store_off(HW_FRAME, sr, off);
}


EMIT(push)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t sr  = hw_local(e.src);
    record(riscv64_ops, "emit_push %u reg %u", e.src, sr);
    emit_push(sr);
}


EMIT(pop)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t dr  = hw_local(e.dst);
    record(riscv64_ops, "emit_pop %u reg %u", e.dst, dr);
    emit_pop(dr);
}


EMIT(sys)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sys opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sys %llu args=%p", e.tok, e.pc);
    dispatch_sys(e);
}


EMIT(sto)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sto opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t  r     = hw_local(e.src);
    uintptr_t gaddr = uintptr_t(&rt.globals[e.dst]);
    load_ptr_imm(HW_IP, uint64_t(gaddr));
    emit_store_off(HW_IP, r, 0);
    record(riscv64_ops, "store_global g[%u]=%u addr %p reg %u",
           e.dst, e.src, gaddr, r);
}


EMIT(rcl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(e.dst);
    uintptr_t gaddr = uintptr_t(&rt.globals[e.src]);
    load_ptr_imm(HW_IP, uint64_t(gaddr));
    emit_load_off(r, HW_IP, 0);
    record(riscv64_ops, "load_global g[%u]=%u addr %p reg %u",
           e.dst, e.src, gaddr, r);
}


EMIT(dbra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
{
    riscv64reg_t r = hw_local(e.dst);
    record(riscv64_ops, "emit_dbra %u, %lld reg %u", e.dst, e.broff, r);
    emit32le(addi(xn(r), xn(r), -1));
    // Branch if non-zero: beq r, x0, +8; j target
    emit32le(beq_imm(xn(r), 0, 8));
    hwcode_t pos = cursor();
    emit32le(j_imm(0));
    rel_branch(e.bpc + e.broff, pos);
}


EMIT(bra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bra opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bra %lld + %lld -> %lld",
           e.bpc, e.broff, e.bpc+e.broff);
    hwcode_t site = cursor();
    emit32le(j_imm(0));
    rel_branch(e.bpc + e.broff, site);
    e.pc = e.end;               // Pick up next translation spot
}


EMIT(btrue)
// ----------------------------------------------------------------------------
//   Emit bytecode for the btrue opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_btrue(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.bpc + e.broff, true);
}


EMIT(bfalse)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bfalse opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bfalse(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.bpc + e.broff, false);
}


EMIT(beq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the beq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_beq(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_EQ);
}


EMIT(bne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bne(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_NE);
}


EMIT(blt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the blt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_blt(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_LT);
}


EMIT(bgt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bgt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bgt(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_GT);
}


EMIT(ble)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ble opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ble(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_LE);
}


EMIT(bge)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bge opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bge(%u, %lld)", e.src, e.broff);
    bra_test(e.src, e.bpc + e.broff, COND_GE);
}


EMIT(switch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the switch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_switch(%u)", e.src);
    dispatch(e.src, e.pc, COND_EQ);
}


EMIT(dispatch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dispatch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dispatch(%u)", e.src);
    dispatch(e.src, e.pc, COND_GE);
}


EMIT(true)
// ----------------------------------------------------------------------------
//   Emit bytecode for the true opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_true(%u)", e.pred);
    set_pred(e.pred, true);
}


EMIT(false)
// ----------------------------------------------------------------------------
//   Emit bytecode for the false opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_false(%u)", e.pred);
    set_pred(e.pred, false);
}


EMIT(cmpeq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeq(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_EQ);
}


EMIT(cmpne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpne(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_NE);
}


EMIT(cmplt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplt(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_LT);
}


EMIT(cmple)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmple opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmple(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_LE);
}


EMIT(cmpult)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpult opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpult(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_CC);
}


EMIT(cmpule)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpule opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpule(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, COND_LS);
}


EMIT(cmpeqi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeqi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeqi(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_EQ);
}


EMIT(cmpnei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpnei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpnei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_NE);
}


EMIT(cmplti)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplti opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplti(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_LT);
}


EMIT(cmplei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, COND_LE);
}


EMIT(cmpequ)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpequ opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpequ(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_EQ);
}


EMIT(cmpneu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpneu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpneu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_NE);
}


EMIT(cmpltu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpltu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpltu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_CC);
}


EMIT(cmpleu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpleu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpleu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, COND_LS);
}


EMIT(pushf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pushf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pushf()");
    emit_push(HW_FLAGS);
}


EMIT(popf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the popf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_popf()");
    emit_pop(HW_FLAGS);
}


EMIT(add)
// ----------------------------------------------------------------------------
//   Emit bytecode for the add opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_add(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_add);
}


EMIT(sub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_sub);
}


EMIT(mul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_mul);
}


EMIT(umul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_umul);
}


EMIT(div)
// ----------------------------------------------------------------------------
//   Emit bytecode for the div opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_div(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_div);
}


EMIT(udiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the udiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_udiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_udiv);
}


EMIT(mod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_mod);
}


EMIT(umod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_umod);
}


EMIT(and)
// ----------------------------------------------------------------------------
//   Emit bytecode for the and opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_and(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_and);
}


EMIT(or)
// ----------------------------------------------------------------------------
//   Emit bytecode for the or opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_or(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_or);
}


EMIT(xor)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xor opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xor(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_xor);
}


EMIT(shl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shl(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_shl);
}


EMIT(shr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_shr);
}


EMIT(ushr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_ushr);
}


EMIT(neg)
// ----------------------------------------------------------------------------
//   Emit bytecode for the neg opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_neg(%u)", e.dst);
    neg_local(e.dst);
}


EMIT(not)
// ----------------------------------------------------------------------------
//   Emit bytecode for the not opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_not(%u)", e.dst);
    not_local(e.dst);
}


EMIT(incr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the incr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_incr(%u)", e.dst);
    incr_local(e.dst);
}


EMIT(decr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the decr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_decr(%u)", e.dst);
    decr_local(e.dst);
}


EMIT(addi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_addi);
}


EMIT(addu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_addu);
}


EMIT(subi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_subi);
}


EMIT(subu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_subu);
}


EMIT(muli)
// ----------------------------------------------------------------------------
//   Emit bytecode for the muli opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_muli(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_muli);
}


EMIT(mulu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mulu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mulu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_mulu);
}


EMIT(divi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_divi);
}


EMIT(divu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_divu);
}


EMIT(modi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, op_modi);
}


EMIT(modu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_modu);
}


EMIT(andu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the andu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_andu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_andu);
}


EMIT(oru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the oru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_oru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_oru);
}


EMIT(xoru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xoru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xoru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, op_xoru);
}


EMIT(shlu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shlu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shlu(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_shlu);
}


EMIT(shru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_shru);
}


EMIT(ushru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, op_ushru);
}


EMIT(st8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 8);
}


EMIT(st4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 4);
}


EMIT(st2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 2);
}


EMIT(st1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 1);
}


EMIT(ld8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 8);
}


EMIT(ld4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 4);
}


EMIT(ld2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 2);
}


EMIT(ld1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 1);
}


EMIT(st8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 8, e.kint);
}


EMIT(st4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 4, e.kint);
}


EMIT(st2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 2, e.kint);
}


EMIT(st1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem(e.dst, e.src, 1, e.kint);
}


EMIT(ld8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 8, e.kint);
}


EMIT(ld4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 4, e.kint);
}


EMIT(ld2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 2, e.kint);
}


EMIT(ld1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem(e.dst, e.src, 1, e.kint);
}


EMIT(lds)
// ----------------------------------------------------------------------------
//   Emit bytecode for the lds opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_lds%u %u=[%u]", e.kshift, e.dst, e.src);
    load_sext(e.dst, e.src, e.kshift);
}


EMIT(ldos)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ldos opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ldos%u %u=[%u+%lld]", e.kshift, e.dst, e.src, e.kint);
    load_sext(e.dst, e.src, e.kshift, e.kint);
}


EMIT(sext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sext(%u, %u)", e.dst, e.kshift);
    sext_local(e.dst, e.kshift);
}


EMIT(zext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the zext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_zext(%u, %u)", e.dst, e.kshift);
    zext_local(e.dst, e.kshift);
}


EMIT(fadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fadd, 32);
}


EMIT(fsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fsub, 32);
}


EMIT(fmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fmul, 32);
}


EMIT(fdiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fdiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_fdiv, 32);
}


EMIT(dadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dadd, 64);
}


EMIT(dsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dsub, 64);
}


EMIT(dmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_dmul, 64);
}


EMIT(ddiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ddiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, op_ddiv, 64);
}


// ============================================================================
//
//   Sys and native opcodes (native.tbl)
//
// ============================================================================

#define SYS(op) void riscv64::emit_sys_##op(emit_args &e)

SYS(globals)
// ----------------------------------------------------------------------------
//   Resize globals table to given length
// ----------------------------------------------------------------------------
{
}


SYS(malloc)
// ----------------------------------------------------------------------------
//   Allocate heap memory of given size
// ----------------------------------------------------------------------------
{
}


SYS(kmalloc)
// ----------------------------------------------------------------------------
//   Allocate heap memory of constant size
// ----------------------------------------------------------------------------
{
}


SYS(realloc)
// ----------------------------------------------------------------------------
//   Resize heap allocation
// ----------------------------------------------------------------------------
{
}


SYS(free)
// ----------------------------------------------------------------------------
//   Free heap allocation
// ----------------------------------------------------------------------------
{
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

    if (bytes > 0 && bytes <= 2047)
        emit32le(addi(xn(HW_SP), xn(HW_SP), -int(bytes)));
    else if (bytes < 0 && -bytes <= 2047)
        emit32le(addi(xn(HW_SP), xn(HW_SP), int(-bytes)));
    else if (bytes != 0)
    {
        load_imm64(HW_IP, uint64_t(bytes));
        emit32le(sub_rr(xn(HW_SP), xn(HW_SP), xn(HW_IP)));
    }
    emit32le(mv_rr(xn(hw_local(e.dst)), xn(HW_SP)));
}

SYS(valloca)
// ----------------------------------------------------------------------------
//   Allocate variable-size stack space, return pointer
// ----------------------------------------------------------------------------
{
    riscv64reg_t sz = hw_local(e.src);
    emit32le(mv_rr(xn(HW_X10), xn(sz)));
    // size = (size > 0 ? size + 15 : size) & ~15
    emit32le(addi(xn(HW_X11), xn(HW_X10), 15));
    emit32le(bge_imm(0, xn(HW_X10), 8)); // size <= 0 → skip mv
    emit32le(mv_rr(xn(HW_X10), xn(HW_X11)));
    load_imm64(HW_IP, ~uint64_t(15));
    emit32le(and_rr(xn(HW_X10), xn(HW_X10), xn(HW_IP)));
    emit32le(sub_rr(xn(HW_SP), xn(HW_SP), xn(HW_X10)));
    emit32le(mv_rr(xn(hw_local(e.dst)), xn(HW_SP)));
}



SYS(memcpy)
// ----------------------------------------------------------------------------
//   Copy memory region
// ----------------------------------------------------------------------------
{
}


SYS(memmove)
// ----------------------------------------------------------------------------
//   Move memory region (overlap-safe)
// ----------------------------------------------------------------------------
{
}


SYS(memset)
// ----------------------------------------------------------------------------
//   Fill memory with byte from register
// ----------------------------------------------------------------------------
{
}


SYS(kmemcpy)
// ----------------------------------------------------------------------------
//   Copy memory region of constant length
// ----------------------------------------------------------------------------
{
}


SYS(kmemmove)
// ----------------------------------------------------------------------------
//   Move memory region of constant length
// ----------------------------------------------------------------------------
{
}


SYS(kmemset)
// ----------------------------------------------------------------------------
//   Fill memory with byte, constant length
// ----------------------------------------------------------------------------
{
}


SYS(memkset)
// ----------------------------------------------------------------------------
//   Fill memory with constant byte value
// ----------------------------------------------------------------------------
{
}


SYS(kmemkset)
// ----------------------------------------------------------------------------
//   Fill memory with constant byte and length
// ----------------------------------------------------------------------------
{
}


SYS(memcmp)
// ----------------------------------------------------------------------------
//   Compare two memory regions
// ----------------------------------------------------------------------------
{
}


SYS(kmemcmp)
// ----------------------------------------------------------------------------
//   Compare memory regions of constant length
// ----------------------------------------------------------------------------
{
}


SYS(memchr)
// ----------------------------------------------------------------------------
//   Find byte in memory region
// ----------------------------------------------------------------------------
{
}


SYS(kmemchr)
// ----------------------------------------------------------------------------
//   Find byte in memory of constant length
// ----------------------------------------------------------------------------
{
}


SYS(memkchr)
// ----------------------------------------------------------------------------
//   Find constant byte in memory region
// ----------------------------------------------------------------------------
{
}


SYS(kmemkchr)
// ----------------------------------------------------------------------------
//   Find constant byte in memory of constant length
// ----------------------------------------------------------------------------
{
}



SYS(strlen)
// ----------------------------------------------------------------------------
//   Compute length of C string
// ----------------------------------------------------------------------------
{
}


SYS(strnlen)
// ----------------------------------------------------------------------------
//   Compute bounded length of C string
// ----------------------------------------------------------------------------
{
}


SYS(kstrnlen)
// ----------------------------------------------------------------------------
//   Compute C string length with constant bound
// ----------------------------------------------------------------------------
{
}


SYS(strcpy)
// ----------------------------------------------------------------------------
//   Copy C string
// ----------------------------------------------------------------------------
{
}


SYS(strncpy)
// ----------------------------------------------------------------------------
//   Copy C string with length bound
// ----------------------------------------------------------------------------
{
}


SYS(kstrncpy)
// ----------------------------------------------------------------------------
//   Copy C string with constant length bound
// ----------------------------------------------------------------------------
{
}


SYS(strcat)
// ----------------------------------------------------------------------------
//   Concatenate C strings
// ----------------------------------------------------------------------------
{
}


SYS(strncat)
// ----------------------------------------------------------------------------
//   Concatenate C strings with length bound
// ----------------------------------------------------------------------------
{
}


SYS(kstrncat)
// ----------------------------------------------------------------------------
//   Concatenate C strings with constant bound
// ----------------------------------------------------------------------------
{
}


SYS(strcmp)
// ----------------------------------------------------------------------------
//   Compare two C strings
// ----------------------------------------------------------------------------
{
}


SYS(strncmp)
// ----------------------------------------------------------------------------
//   Compare C strings with length bound
// ----------------------------------------------------------------------------
{
}


SYS(kstrncmp)
// ----------------------------------------------------------------------------
//   Compare C strings with constant bound
// ----------------------------------------------------------------------------
{
}


SYS(strchr)
// ----------------------------------------------------------------------------
//   Find character in C string
// ----------------------------------------------------------------------------
{
}


SYS(kstrchr)
// ----------------------------------------------------------------------------
//   Find constant character in C string
// ----------------------------------------------------------------------------
{
}


SYS(strrchr)
// ----------------------------------------------------------------------------
//   Find last character in C string
// ----------------------------------------------------------------------------
{
}


SYS(kstrrchr)
// ----------------------------------------------------------------------------
//   Find last constant character in C string
// ----------------------------------------------------------------------------
{
}


SYS(strstr)
// ----------------------------------------------------------------------------
//   Find substring in C string
// ----------------------------------------------------------------------------
{
}


SYS(strcasestr)
// ----------------------------------------------------------------------------
//   Find substring case-insensitively
// ----------------------------------------------------------------------------
{
}


SYS(strdup)
// ----------------------------------------------------------------------------
//   Duplicate C string on heap
// ----------------------------------------------------------------------------
{
}


SYS(strndup)
// ----------------------------------------------------------------------------
//   Duplicate C string on heap with length bound
// ----------------------------------------------------------------------------
{
}


SYS(kstrndup)
// ----------------------------------------------------------------------------
//   Duplicate C string with constant length bound
// ----------------------------------------------------------------------------
{
}



SYS(cmdline)
// ----------------------------------------------------------------------------
//   Get command-line argument by index
// ----------------------------------------------------------------------------
{
}


SYS(env)
// ----------------------------------------------------------------------------
//   Get environment variable by index
// ----------------------------------------------------------------------------
{
}



SYS(infix)
// ----------------------------------------------------------------------------
//   Enter infix parsing mode
// ----------------------------------------------------------------------------
{
}


SYS(prefix)
// ----------------------------------------------------------------------------
//   Enter prefix parsing mode
// ----------------------------------------------------------------------------
{
}


SYS(postfix)
// ----------------------------------------------------------------------------
//   Enter postfix parsing mode
// ----------------------------------------------------------------------------
{
}


SYS(block)
// ----------------------------------------------------------------------------
//   Enter block parsing mode
// ----------------------------------------------------------------------------
{
}


SYS(builtin)
// ----------------------------------------------------------------------------
//   Switch to builtin (non-parsing) mode
// ----------------------------------------------------------------------------
{
}


SYS(parsing)
// ----------------------------------------------------------------------------
//   Switch to parsing mode
// ----------------------------------------------------------------------------
{
}



SYS(branch_on_eof)
// ----------------------------------------------------------------------------
//   Branch if input stream is at EOF
// ----------------------------------------------------------------------------
{
}


SYS(branch_on_bad)
// ----------------------------------------------------------------------------
//   Branch if input stream is bad
// ----------------------------------------------------------------------------
{
}



SYS(read_char)
// ----------------------------------------------------------------------------
//   Read character from input stream
// ----------------------------------------------------------------------------
{
}


SYS(read_integer)
// ----------------------------------------------------------------------------
//   Read integer from input stream
// ----------------------------------------------------------------------------
{
}


SYS(read_natural)
// ----------------------------------------------------------------------------
//   Read natural number from input stream
// ----------------------------------------------------------------------------
{
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
}


SYS(write_integer)
// ----------------------------------------------------------------------------
//   Write integer to output stream
// ----------------------------------------------------------------------------
{
    riscv64reg_t v = hw_local(e.src);
    emit32le(mv_rr(xn(HW_X11), xn(v)));
    load_imm64(HW_X10, e.knat);
    call_ptr((void (*)()) xlr8_jit_write_integer);
}


SYS(write_natural)
// ----------------------------------------------------------------------------
//   Write natural number to output stream
// ----------------------------------------------------------------------------
{
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
}


SYS(write_token)
// ----------------------------------------------------------------------------
//   Write token text to output stream
// ----------------------------------------------------------------------------
{
    load_imm64(HW_X10, e.knat);
    load_imm64(HW_X11, e.tok);
    call_ptr((void (*)()) xlr8_jit_write_token);
}


SYS(write_newline)
// ----------------------------------------------------------------------------
//   Write newline to output stream
// ----------------------------------------------------------------------------
{
    load_imm64(HW_X10, e.knat);
    call_ptr((void (*)()) xlr8_jit_write_newline);
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
