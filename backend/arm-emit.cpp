// ****************************************************************************
//  arm-emit.cpp                                               XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Table-generated opcode emitters for 32-bit ARM
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

#define EMIT(op)        void arm::emit_##op(emit_args &e)

EMIT(null)
// ----------------------------------------------------------------------------
//   Emit bytecode for the null opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_null()");
    trap("null opcode");
}


EMIT(undef)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_undef(%llu)", e.tok);
    bind(e.tok);
}


EMIT(end)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_end");
    translating = false;
}


EMIT(user)
// ----------------------------------------------------------------------------
//   Emit a user-defined opcode (128..255, or registered low opcodes)
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_user(%lld, %p)", e.op, &e.pc);
    byte_p user_args = e.pc;
    spill_frame();
    load_imm32(0, uint32_t(e.op));
    load_imm32(1, uint32_t(uintptr_t(user_args)));
    rtcall("xlr8_user_op");
}


EMIT(int)
// ----------------------------------------------------------------------------
//   Emit bytecode for the int opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_int(%u, %lld)", e.dst, e.kint);
    store_imm(e.dst, value_t(e.kint));
}


EMIT(nat)
// ----------------------------------------------------------------------------
//   Emit bytecode for the nat opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_nat(%u, %llu)", e.dst, e.knat);
    store_imm(e.dst, value_t(e.knat));
}


EMIT(flt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the flt opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_flt(%u, %g)", e.dst, e.kflt);
    store_float(e.dst, e.kflt);
}


EMIT(dbl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbl opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_dbl(%u, %g)", e.dst, e.kdbl);
    store_double(e.dst, e.kdbl);
}


EMIT(txt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the txt opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_txt(%u, %llu)", e.dst, e.tok);
    store_imm(e.dst, value_t(e.tok));
}


EMIT(copy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the copy opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_copy(%u, %u)", e.dst, e.src);
    mov_local(e.dst, e.src);
}


EMIT(swap)
// ----------------------------------------------------------------------------
//   Emit bytecode for the swap opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_swap(%u, %u)", e.dst, e.src);
    swap_locals(e.dst, e.src);
}


EMIT(ccopy)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ccopy opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ccopy(%u, %u, %u)", e.pred, e.dst, e.src);
    cond_mov_local(e.pred, e.dst, e.src);
}


EMIT(select)
// ----------------------------------------------------------------------------
//   Emit bytecode for the select opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_select(%u, %u, %u, %u)", e.pred, e.dst, e.src, e.xtra);
    select_local(e.pred, e.dst, e.src, e.xtra);
}


EMIT(jmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the jmp opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_jmp(%llu)", e.tok);
    jmp_tok(e.tok);
}


EMIT(call)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_call(%llu)", e.tok);
    call_tok(e.tok);
}


EMIT(ijmp)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ijmp(%u)", e.src);
    ijmp_local(e.src);
}


EMIT(icall)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_icall(%u)", e.src);
    icall_local(e.src);
}


EMIT(ret)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ret opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ret()");
    // Bytecode ret: soft-stack return address, not the JIT epilogue (epilogue).
    rtcall("xlr8_pop_value");
    br_via_ip();
}


EMIT(ppc)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ppc opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ppc()");
    push_pc();
}


EMIT(pc)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pc opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_pc(%u)", e.dst);
    load_pc(e.dst);
}


EMIT(pca)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pca opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_pca(%u, %lld)", e.dst, e.broff);
    load_pca(e.dst, e.broff);
}


EMIT(enter)
// ----------------------------------------------------------------------------
//   Emit bytecode for the enter opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_enter(%u)", e.dst);
    spill_arg_regs = true;
    spill_frame();
    emit32le(mov_rr(0, HW_FLAGS));
    emit32le(movw(1, 0));
    rtcall("xlr8_set_flags");
    load_imm32(0, uint32_t(e.dst));
    rtcall("xlr8_enter");
    reload_frame_reg();
}


EMIT(leave)
// ----------------------------------------------------------------------------
//   Emit bytecode for the leave opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_leave()");
    rtcall("xlr8_leave");
    reload_frame_reg();
    reload_flags_reg();
}


EMIT(get)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_get(%u, %u)", e.dst, e.src);
    get_arg(e.dst, e.src);
}


EMIT(set)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_set(%u, %u)", e.dst, e.src);
    set_arg(e.dst, e.src);
}


EMIT(push)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_push(%u)", e.src);
    push_local(e.src);
}


EMIT(pop)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_pop(%u)", e.dst);
    pop_local(e.dst);
}


EMIT(sys)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sys opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_sys(%llu, %p, %p)", e.tok, e.pc, e.pc);
    spill_arg_regs = true;
    spill_frame();
    emit32le(mov_rr(0, HW_FLAGS));
    emit32le(movw(1, 0)); // uint64_t flags arg high half (AAPCS r0:r1)
    rtcall("xlr8_set_flags");
    load_imm32(0, uint32_t(e.tok));
    load_imm32(1, uint32_t(uintptr_t(e.pc)));
    call_abs((void *) xlr8_sys);
    // Linux may move rt.frame during natives; keep r11 in sync (cf. arm64).
    rtcall("xlr8_get_frame");
    emit32le(mov_rr(HW_FRAME, 0));
    reload_flags_reg();
    load_imm32(0, uint32_t(uintptr_t(e.pc)));
    rtcall("xlr8_after_sys");
    // Arm32 has no CBNZ; cmp sets Z from r0, then BNE matches arm64 CBNZ x0.
    emit32le(cmp_r_imm(0, 0));
    uint cbnz_at = uint(cursor());
    emit32le(b_cond_imm(0x1, 0));
    spill_arg_regs = false;
    uint skip_at = uint(cursor());
    emit32le(b_imm(0));
    uint br_at = uint(cursor());
    emit32le(mov_rr(HW_IP, 0));
    emit32le(bx_reg(HW_IP));
    code().patch32le(cbnz_at, b_cond_imm(0x1, int(br_at - cbnz_at - 8) / 4));
    code().patch32le(skip_at, b_imm(int(br_at + 4 - skip_at - 8) / 4));
}


EMIT(sto)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sto opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_sto(%u, %u)", e.dst, e.src);
    store_global(e.dst, e.src);
}


EMIT(rcl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_rcl(%u, %u)", e.dst, e.src);
    load_global(e.dst, e.src);
}


EMIT(dbra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_dbra(%u, %lld)", e.dst, e.broff);
    dbra_local(e.dst, e.broff);
}


EMIT(bra)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bra opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_bra(%lld)", e.broff);
    bra_offset(e.broff);
}


EMIT(btrue)
// ----------------------------------------------------------------------------
//   Emit bytecode for the btrue opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_btrue(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.broff, true);
}


EMIT(bfalse)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bfalse opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_bfalse(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.broff, false);
}


EMIT(beq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the beq opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_beq(%u, %lld)", e.src, e.broff);
    bra_zero(e.src, e.broff, true);
}


EMIT(bne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bne opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_bne(%u, %lld)", e.src, e.broff);
    bra_zero(e.src, e.broff, false);
}


EMIT(blt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the blt opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_blt(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xb);
}


EMIT(bgt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bgt opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_bgt(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xc);
}


EMIT(ble)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ble opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ble(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xd);
}


EMIT(bge)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bge opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_bge(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xa);
}


EMIT(switch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the switch opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_switch(%u)", e.src);
    rtcall("xlr8_switch", e.src);
}


EMIT(dispatch)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dispatch opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_dispatch(%u, %p)", e.src, e.pc);
    spill_frame();
    load_imm32(0, uint32_t(e.src));
    load_imm32(1, uint32_t(uintptr_t(e.pc)));
    rtcall("xlr8_dispatch");
    br_via_ip();
}


EMIT(true)
// ----------------------------------------------------------------------------
//   Emit bytecode for the true opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_true(%u)", e.pred);
    set_pred(e.pred, true);
}


EMIT(false)
// ----------------------------------------------------------------------------
//   Emit bytecode for the false opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_false(%u)", e.pred);
    set_pred(e.pred, false);
}


EMIT(cmpeq)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeq opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpeq(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "eq");
}


EMIT(cmpne)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpne opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpne(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "ne");
}


EMIT(cmplt)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplt opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmplt(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "lt");
}


EMIT(cmple)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmple opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmple(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "le");
}


EMIT(cmpult)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpult opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpult(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "ltu");
}


EMIT(cmpule)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpule opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpule(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "leu");
}


EMIT(cmpeqi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeqi opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpeqi(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "eq");
}


EMIT(cmpnei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpnei opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpnei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "ne");
}


EMIT(cmplti)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplti opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmplti(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "lt");
}


EMIT(cmplei)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplei opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmplei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "le");
}


EMIT(cmpequ)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpequ opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpequ(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "eq");
}


EMIT(cmpneu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpneu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpneu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "ne");
}


EMIT(cmpltu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpltu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpltu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "ltu");
}


EMIT(cmpleu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpleu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_cmpleu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "leu");
}


EMIT(pushf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pushf opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_pushf()");
    push_flags();
}


EMIT(popf)
// ----------------------------------------------------------------------------
//   Emit bytecode for the popf opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_popf()");
    pop_flags();
}


EMIT(add)
// ----------------------------------------------------------------------------
//   Emit bytecode for the add opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_add(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "add");
}


EMIT(sub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sub opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_sub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "sub");
}


EMIT(mul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mul opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_mul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "imul");
}


EMIT(umul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umul opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_umul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "imul");
}


EMIT(div)
// ----------------------------------------------------------------------------
//   Emit bytecode for the div opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_div(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "idiv");
}


EMIT(udiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the udiv opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_udiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "div");
}


EMIT(mod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mod opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_mod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "idiv_mod");
}


EMIT(umod)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umod opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_umod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "div_mod");
}


EMIT(and)
// ----------------------------------------------------------------------------
//   Emit bytecode for the and opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_and(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "and");
}


EMIT(or)
// ----------------------------------------------------------------------------
//   Emit bytecode for the or opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_or(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "or");
}


EMIT(xor)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xor opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_xor(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "xor");
}


EMIT(shl)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shl opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_shl(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "shl");
}


EMIT(shr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shr opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_shr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "sar");
}


EMIT(ushr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushr opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ushr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "shr");
}


EMIT(neg)
// ----------------------------------------------------------------------------
//   Emit bytecode for the neg opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_neg(%u)", e.dst);
    neg_local(e.dst);
}


EMIT(not)
// ----------------------------------------------------------------------------
//   Emit bytecode for the not opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_not(%u)", e.dst);
    not_local(e.dst);
}


EMIT(incr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the incr opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_incr(%u)", e.dst);
    incr_local(e.dst);
}


EMIT(decr)
// ----------------------------------------------------------------------------
//   Emit bytecode for the decr opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_decr(%u)", e.dst);
    decr_local(e.dst);
}


EMIT(addi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addi opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_addi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "add");
}


EMIT(addu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_addu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "add");
}


EMIT(subi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subi opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_subi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "sub");
}


EMIT(subu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_subu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "sub");
}


EMIT(muli)
// ----------------------------------------------------------------------------
//   Emit bytecode for the muli opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_muli(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "imul");
}


EMIT(mulu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mulu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_mulu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "imul");
}


EMIT(divi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divi opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_divi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "idiv_imm");
}


EMIT(divu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_divu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "div_imm");
}


EMIT(modi)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modi opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_modi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "idiv_mod_imm");
}


EMIT(modu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_modu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "div_mod_imm");
}


EMIT(andu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the andu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_andu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "and");
}


EMIT(oru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the oru opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_oru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "or");
}


EMIT(xoru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xoru opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_xoru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "xor");
}


EMIT(shlu)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shlu opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_shlu(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, "shl_imm");
}


EMIT(shru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shru opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_shru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, "sar_imm");
}


EMIT(ushru)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushru opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ushru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, "shr_imm");
}


EMIT(frame)
// ----------------------------------------------------------------------------
//   Emit bytecode for the frame opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_frame(%u, %u)", e.dst, e.src);
    frame_addr(e.dst, e.src);
}


EMIT(st8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st8(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 8);
}


EMIT(st4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st4(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 4);
}


EMIT(st2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st2(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 2);
}


EMIT(st1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st1(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 1);
}


EMIT(ld8)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld8(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 8);
}


EMIT(ld4)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld4(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 4);
}


EMIT(ld2)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld2(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 2);
}


EMIT(ld1)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1 opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld1(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 1);
}


EMIT(st8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 8);
}


EMIT(st4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 4);
}


EMIT(st2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 2);
}


EMIT(st1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_st1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 1);
}


EMIT(ld8o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 8);
}


EMIT(ld4o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 4);
}


EMIT(ld2o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 2);
}


EMIT(ld1o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1o opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ld1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 1);
}


EMIT(lds)
// ----------------------------------------------------------------------------
//   Emit bytecode for the lds opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_lds(%u, %u, %u)", e.dst, e.src, e.kshift);
    load_signext(e.dst, e.src, e.kshift);
}


EMIT(ldos)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ldos opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ldos(%u, %u, %u, %lld)", e.dst, e.src, e.kshift, e.kint);
    load_signext_off(e.dst, e.src, e.kshift, e.kint);
}


EMIT(sext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sext opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_sext(%u, %u)", e.dst, e.kshift);
    sext_local(e.dst, e.kshift);
}


EMIT(zext)
// ----------------------------------------------------------------------------
//   Emit bytecode for the zext opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_zext(%u, %u)", e.dst, e.kshift);
    zext_local(e.dst, e.kshift);
}


EMIT(fadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fadd opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_fadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fadd");
}


EMIT(fsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_fsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fsub");
}


EMIT(fmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_fmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fmul");
}


EMIT(fdiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_fdiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fdiv");
}


EMIT(dadd)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_dadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "dadd");
}


EMIT(dsub)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_dsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "dsub");
}


EMIT(dmul)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_dmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "dmul");
}


EMIT(ddiv)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(arm_ops, "emit_ddiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "ddiv");
}


// ============================================================================
//
//   Sys and native opcodes (native.tbl)
//
// ============================================================================

#define SYS(op) void arm::emit_sys_##op(emit_args &e)

SYS(globals)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(malloc)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmalloc)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(realloc)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(free)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(alloca)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(valloca)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(memcpy)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(memmove)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(memset)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmemcpy)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmemmove)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmemset)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(memkset)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmemkset)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(memcmp)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmemcmp)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(memchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmemchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(memkchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kmemkchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(strlen)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strnlen)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrnlen)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strcpy)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strncpy)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrncpy)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strcat)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strncat)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrncat)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strcmp)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strncmp)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrncmp)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strrchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrrchr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strstr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


#if 0 // does not exist on Debian GNU/Linux 12 (bookworm)
SYS(strnstr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrnstr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


#endif // Debian
SYS(strcasestr)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strdup)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(strndup)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(kstrndup)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(cmdline)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(env)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(infix)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(prefix)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(postfix)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(block)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(builtin)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(parsing)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(branch_on_eof)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(branch_on_bad)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(read_char)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(read_integer)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(read_natural)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(read_float)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(read_double)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(write_char)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(write_integer)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(write_natural)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(write_float)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{

}


SYS(write_double)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(write_text)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(write_token)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(write_newline)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(read_scanner)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(nat)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(int)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(flt)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(dbl)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(print_error)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(clear_error)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(trap_error)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(i2f)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(i2d)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(u2f)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(u2d)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(f2i)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(d2i)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(f2u)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(d2u)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(f2d)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(d2f)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(fpush)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(dpush)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(fpop)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(dpop)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(fmod)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(dmod)
// ----------------------------------------------------------------------------
//
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
//
// ----------------------------------------------------------------------------
{
}



SYS(st8v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(st4v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(st2v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(st1v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld8v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld4v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld2v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld1v)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld8vs)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld4vs)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld2vs)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(ld1vs)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}



SYS(tok)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(leb2i)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(leb2u)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(i2leb)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(u2leb)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(i2lebsz)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(u2lebsz)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}


SYS(cstring)
// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------
{
}
