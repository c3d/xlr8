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


void riscv64::emit_null(emit_args &e)
// ----------------------------------------------------------------------------
//   Null opcode in inline data: skip during translation (no native code)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_null()");
}


void riscv64::emit_user(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit a user-defined opcode (128..255, or registered low opcodes)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_user(%lld, %p)", e.op, &e.pc);
    byte_p user_args = e.pc;
    spill_frame();
    load_imm64(10, uint64_t(e.op));
    load_imm64(11, uint64_t(uintptr_t(user_args)));
    rtcall("xlr8_user_op");
    reload_shadows();
}


void riscv64::emit_undef(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_undef(%llu)", e.tok);
    bind(e.tok);
}


void riscv64::emit_end(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_end");
    translating = false;
}


void riscv64::emit_int(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the int opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_int(%u, %lld)", e.dst, e.kint);
    store_imm(e.dst, value_t(e.kint));
}


void riscv64::emit_nat(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the nat opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_nat(%u, %llu)", e.dst, e.knat);
    store_uimm(e.dst, e.knat);
}


void riscv64::emit_flt(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the flt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_flt(%u, %g)", e.dst, e.kflt);
    store_float(e.dst, e.kflt);
}


void riscv64::emit_dbl(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dbl(%u, %g)", e.dst, e.kdbl);
    store_double(e.dst, e.kdbl);
}


void riscv64::emit_txt(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the txt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_txt(%u, %llu)", e.dst, e.tok);
    store_imm(e.dst, value_t(e.tok));
}


void riscv64::emit_copy(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the copy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_copy(%u, %u)", e.dst, e.src);
    mov_local(e.dst, e.src);
}


void riscv64::emit_swap(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the swap opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_swap(%u, %u)", e.dst, e.src);
    swap_locals(e.dst, e.src);
}


void riscv64::emit_ccopy(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ccopy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ccopy(%u, %u, %u)", e.pred, e.dst, e.src);
    cond_mov_local(e.pred, e.dst, e.src);
}


void riscv64::emit_select(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the select opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_select(%u, %u, %u, %u)", e.pred, e.dst, e.src, e.xtra);
    select_local(e.pred, e.dst, e.src, e.xtra);
}


void riscv64::emit_jmp(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the jmp opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_jmp(%llu)", e.tok);
    jmp_tok(e.tok);
}


void riscv64::emit_call(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_call(%llu)", e.tok);
    call_tok(e.tok);
}


void riscv64::emit_ijmp(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ijmp(%u)", e.src);
    ijmp_local(e.src);
}


void riscv64::emit_icall(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_icall(%u)", e.src);
    icall_local(e.src);
}


void riscv64::emit_ret(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ret opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ret()");
    rtcall("xlr8_pop_value");
    emit32le(mv_rr(HW_SCR_VAL, 10));
    reload_shadows();
    emit32le(jalr(HW_SCR_VAL));
}


void riscv64::emit_ppc(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ppc opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ppc()");
    push_pc();
}


void riscv64::emit_pc(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pc opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pc(%u)", e.dst);
    load_pc(e.dst);
}


void riscv64::emit_pca(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pca opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pca(%u, %lld)", e.dst, e.broff);
    load_pca(e.dst, e.broff);
}


void riscv64::emit_enter(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the enter opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_enter(%u)", e.dst);
    spill_frame();
    emit32le(mv_rr(10, HW_FLAGS));
    rtcall("xlr8_set_flags");
    rtcall("xlr8_enter", e.dst);
    reload_frame_reg();
    reload_shadows();
}


void riscv64::emit_leave(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the leave opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_leave()");
    rtcall("xlr8_leave");
    reload_frame_reg();
    reload_flags_reg();
    reload_shadows();
}


void riscv64::emit_get(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_get(%u, %u)", e.dst, e.src);
    get_arg(e.dst, e.src);
}


void riscv64::emit_set(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_set(%u, %u)", e.dst, e.src);
    set_arg(e.dst, e.src);
}


void riscv64::emit_push(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_push(%u)", e.src);
    push_local(e.src);
}


void riscv64::emit_pop(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pop(%u)", e.dst);
    pop_local(e.dst);
}


void riscv64::emit_sys(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sys opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sys(%llu, %p, %p)", e.tok, e.pc, e.pc);
    spill_arg_regs = true;
    spill_frame();
    emit32le(mv_rr(10, HW_FLAGS));
    rtcall("xlr8_set_flags");
    load_imm64(10, uint64_t(e.tok));
    load_imm64(11, uint64_t(uintptr_t(e.pc)));
    call_abs((void *) xlr8_sys);
    // Linux helpers may move rt.frame (enter/leave/alloca); refresh s0 like
    // arm64'e.src post-sys xlr8_get_frame path so reload_shadows is not stale.
    rtcall("xlr8_get_frame");
    emit32le(reload_frame());
    // get_flags clobbers a0-a7 (shadow locals 0-7); reload shadows after it.
    reload_flags_reg();
    reload_shadows();
    load_imm64(HW_SCR_VAL, uint64_t(uintptr_t(e.pc)));
    emit32le(mv_rr(10, HW_SCR_VAL));
    rtcall("xlr8_after_sys");
    // after_sys returns 0 to continue linearly, else a native continue address.
    // Match x86: skip the indirect jump entirely when the result is 0.
    // Do not call_abs between mv HW_IP and jalr — call_abs clobbers HW_IP (t0).
    uint bne_at = uint(cursor());
    emit32le(bne_imm(10, 0, 0));
    reload_shadows();
    spill_arg_regs = false;
    uint skip_at = uint(cursor());
    emit32le(j_imm(0));
    uint br_at = uint(cursor());
    emit32le(mv_rr(HW_IP, 10));
    reload_shadows();
    emit32le(jalr(HW_IP));
    uint end_at = uint(cursor());
    code().patch32(bne_at, bne_imm(10, 0, int(br_at) - int(bne_at)));
    code().patch32(skip_at, j_imm(int(end_at) - int(skip_at)));
}


void riscv64::emit_sto(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sto opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sto(%u, %u)", e.dst, e.src);
    store_global(e.dst, e.src);
}


void riscv64::emit_rcl(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_rcl(%u, %u)", e.dst, e.src);
    load_global(e.dst, e.src);
}


void riscv64::emit_dbra(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dbra(%u, %lld)", e.dst, e.broff);
    dbra_local(e.dst, e.broff);
}


void riscv64::emit_bra(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bra opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bra(%lld)", e.broff);
    bra_offset(e.broff);
}


void riscv64::emit_btrue(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the btrue opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_btrue(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.broff, true);
}


void riscv64::emit_bfalse(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bfalse opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bfalse(%u, %lld)", e.pred, e.broff);
    bra_pred(e.pred, e.broff, false);
}


void riscv64::emit_beq(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the beq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_beq(%u, %lld)", e.src, e.broff);
    bra_zero(e.src, e.broff, true);
}


void riscv64::emit_bne(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bne(%u, %lld)", e.src, e.broff);
    bra_zero(e.src, e.broff, false);
}


void riscv64::emit_blt(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the blt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_blt(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xb);
}


void riscv64::emit_bgt(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bgt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bgt(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xc);
}


void riscv64::emit_ble(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ble opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ble(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xd);
}


void riscv64::emit_bge(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bge opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bge(%u, %lld)", e.src, e.broff);
    bra_signed(e.src, e.broff, 0xa);
}


void riscv64::emit_switch(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the switch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_switch(%u)", e.src);
    rtcall("xlr8_switch", e.src);
}


void riscv64::emit_dispatch(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dispatch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dispatch(%u, %p)", e.src, e.pc);
    spill_frame();
    load_imm64(10, uint64_t(e.src));
    load_imm64(11, uint64_t(uintptr_t(e.pc)));
    rtcall("xlr8_dispatch");
    br_via_ip();
}


void riscv64::emit_true(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the true opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_true(%u)", e.pred);
    set_pred(e.pred, true);
}


void riscv64::emit_false(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the false opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_false(%u)", e.pred);
    set_pred(e.pred, false);
}


void riscv64::emit_cmpeq(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeq(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "eq");
}


void riscv64::emit_cmpne(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpne(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "ne");
}


void riscv64::emit_cmplt(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplt(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "lt");
}


void riscv64::emit_cmple(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmple opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmple(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "le");
}


void riscv64::emit_cmpult(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpult opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpult(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "ltu");
}


void riscv64::emit_cmpule(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpule opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpule(%u, %u, %u)", e.pred, e.dst, e.src);
    cmp_local(e.pred, e.dst, e.src, "leu");
}


void riscv64::emit_cmpeqi(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeqi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeqi(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "eq");
}


void riscv64::emit_cmpnei(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpnei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpnei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "ne");
}


void riscv64::emit_cmplti(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplti opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplti(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "lt");
}


void riscv64::emit_cmplei(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplei(%u, %u, %lld)", e.pred, e.dst, e.kint);
    cmp_imm(e.pred, e.dst, e.kint, "le");
}


void riscv64::emit_cmpequ(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpequ opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpequ(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "eq");
}


void riscv64::emit_cmpneu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpneu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpneu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "ne");
}


void riscv64::emit_cmpltu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpltu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpltu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "ltu");
}


void riscv64::emit_cmpleu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpleu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpleu(%u, %u, %llu)", e.pred, e.dst, e.knat);
    cmp_uimm(e.pred, e.dst, e.knat, "leu");
}


void riscv64::emit_pushf(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pushf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pushf()");
    push_flags();
}


void riscv64::emit_popf(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the popf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_popf()");
    pop_flags();
}


void riscv64::emit_add(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the add opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_add(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "add");
}


void riscv64::emit_sub(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "sub");
}


void riscv64::emit_mul(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "imul");
}


void riscv64::emit_umul(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "imul");
}


void riscv64::emit_div(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the div opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_div(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "idiv");
}


void riscv64::emit_udiv(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the udiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_udiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "div");
}


void riscv64::emit_mod(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "idiv_mod");
}


void riscv64::emit_umod(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umod(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "div_mod");
}


void riscv64::emit_and(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the and opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_and(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "and");
}


void riscv64::emit_or(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the or opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_or(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "or");
}


void riscv64::emit_xor(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xor opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xor(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "xor");
}


void riscv64::emit_shl(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shl(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "shl");
}


void riscv64::emit_shr(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "sar");
}


void riscv64::emit_ushr(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushr(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "shr");
}


void riscv64::emit_neg(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the neg opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_neg(%u)", e.dst);
    neg_local(e.dst);
}


void riscv64::emit_not(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the not opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_not(%u)", e.dst);
    not_local(e.dst);
}


void riscv64::emit_incr(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the incr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_incr(%u)", e.dst);
    incr_local(e.dst);
}


void riscv64::emit_decr(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the decr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_decr(%u)", e.dst);
    decr_local(e.dst);
}


void riscv64::emit_addi(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "add");
}


void riscv64::emit_addu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "add");
}


void riscv64::emit_subi(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "sub");
}


void riscv64::emit_subu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "sub");
}


void riscv64::emit_muli(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the muli opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_muli(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "imul");
}


void riscv64::emit_mulu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mulu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mulu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "imul");
}


void riscv64::emit_divi(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "idiv_imm");
}


void riscv64::emit_divu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "div_imm");
}


void riscv64::emit_modi(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modi(%u, %lld)", e.dst, e.kint);
    binop_imm(e.dst, e.kint, "idiv_mod_imm");
}


void riscv64::emit_modu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "div_mod_imm");
}


void riscv64::emit_andu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the andu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_andu(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "and");
}


void riscv64::emit_oru(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the oru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_oru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "or");
}


void riscv64::emit_xoru(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xoru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xoru(%u, %llu)", e.dst, e.knat);
    binop_imm(e.dst, e.knat, "xor");
}


void riscv64::emit_shlu(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shlu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shlu(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, "shl_imm");
}


void riscv64::emit_shru(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, "sar_imm");
}


void riscv64::emit_ushru(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushru(%u, %u)", e.dst, e.kshift);
    binop_imm(e.dst, e.kshift, "shr_imm");
}


void riscv64::emit_frame(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the frame opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_frame(%u, %u)", e.dst, e.src);
    frame_addr(e.dst, e.src);
}


void riscv64::emit_st8(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 8);
}


void riscv64::emit_st4(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 4);
}


void riscv64::emit_st2(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 2);
}


void riscv64::emit_st1(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1(%u, %u)", e.dst, e.src);
    store_mem(e.dst, e.src, 1);
}


void riscv64::emit_ld8(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 8);
}


void riscv64::emit_ld4(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 4);
}


void riscv64::emit_ld2(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 2);
}


void riscv64::emit_ld1(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1(%u, %u)", e.dst, e.src);
    load_mem(e.dst, e.src, 1);
}


void riscv64::emit_st8o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 8);
}


void riscv64::emit_st4o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 4);
}


void riscv64::emit_st2o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 2);
}


void riscv64::emit_st1o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    store_mem_off(e.dst, e.src, e.kint, 1);
}


void riscv64::emit_ld8o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 8);
}


void riscv64::emit_ld4o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 4);
}


void riscv64::emit_ld2o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 2);
}


void riscv64::emit_ld1o(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1o(%u, %u, %lld)", e.dst, e.src, e.kint);
    load_mem_off(e.dst, e.src, e.kint, 1);
}


void riscv64::emit_lds(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the lds opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_lds(%u, %u, %u)", e.dst, e.src, e.kshift);
    load_signext(e.dst, e.src, e.kshift);
}


void riscv64::emit_ldos(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ldos opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ldos(%u, %u, %u, %lld)", e.dst, e.src, e.kshift, e.kint);
    load_signext_off(e.dst, e.src, e.kshift, e.kint);
}


void riscv64::emit_sext(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sext(%u, %u)", e.dst, e.kshift);
    sext_local(e.dst, e.kshift);
}


void riscv64::emit_zext(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the zext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_zext(%u, %u)", e.dst, e.kshift);
    zext_local(e.dst, e.kshift);
}


void riscv64::emit_fadd(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fadd");
}


void riscv64::emit_fsub(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fsub");
}


void riscv64::emit_fmul(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fmul");
}


void riscv64::emit_fdiv(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fdiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "fdiv");
}


void riscv64::emit_dadd(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dadd(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "dadd");
}


void riscv64::emit_dsub(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dsub(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "dsub");
}


void riscv64::emit_dmul(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dmul(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "dmul");
}


void riscv64::emit_ddiv(emit_args &e)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ddiv(%u, %u)", e.dst, e.src);
    binop_local(e.dst, e.src, "ddiv");
}


// ============================================================================
//
//   Sys and native opcodes (native.tbl)
//
// ============================================================================

#define SYS(op) void riscv64::emit_sys_##op(emit_args &e)

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
