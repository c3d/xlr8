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


void riscv64::emit_null()
// ----------------------------------------------------------------------------
//   Null opcode in inline data: skip during translation (no native code)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_null()");
}


void riscv64::emit_user(op_t op, byte_p &pc)
// ----------------------------------------------------------------------------
//   Emit a user-defined opcode (128..255, or registered low opcodes)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_user(%lld, %p)", op, &pc);
    byte_p args = pc;
    spill_frame();
    load_imm64(10, uint64_t(op));
    load_imm64(11, uint64_t(uintptr_t(args)));
    rtcall("xlr8_user_op");
    reload_shadows();
    xlr8_skip_user_args(op, pc);
}


void riscv64::emit_undef(tok_t t)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_undef(%llu)", t);
    bind(t);
}


void riscv64::emit_int(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the int opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_int(%u, %lld)", d, v);
    store_imm(d, value_t(v));
}


void riscv64::emit_nat(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the nat opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_nat(%u, %llu)", d, v);
    store_uimm(d, v);
}


void riscv64::emit_flt(byte d, float v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the flt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_flt(%u, %g)", d, v);
    store_float(d, v);
}


void riscv64::emit_dbl(byte d, double v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dbl(%u, %g)", d, v);
    store_double(d, v);
}


void riscv64::emit_txt(byte d, tok_t t)
// ----------------------------------------------------------------------------
//   Emit bytecode for the txt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_txt(%u, %llu)", d, t);
    store_imm(d, value_t(t));
}


void riscv64::emit_copy(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the copy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_copy(%u, %u)", d, s);
    mov_local(d, s);
}


void riscv64::emit_swap(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the swap opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_swap(%u, %u)", d, s);
    swap_locals(d, s);
}


void riscv64::emit_ccopy(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ccopy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ccopy(%u, %u, %u)", p, d, s);
    cond_mov_local(p, d, s);
}


void riscv64::emit_select(byte p, byte d, byte s, byte x)
// ----------------------------------------------------------------------------
//   Emit bytecode for the select opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_select(%u, %u, %u, %u)", p, d, s, x);
    select_local(p, d, s, x);
}


void riscv64::emit_jmp(tok_t t)
// ----------------------------------------------------------------------------
//   Emit bytecode for the jmp opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_jmp(%llu)", t);
    jmp_tok(t);
}


void riscv64::emit_call(tok_t t)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_call(%llu)", t);
    call_tok(t);
}


void riscv64::emit_ijmp(byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ijmp(%u)", s);
    ijmp_local(s);
}


void riscv64::emit_icall(byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_icall(%u)", s);
    icall_local(s);
}


void riscv64::emit_ret()
// ----------------------------------------------------------------------------
//   Emit bytecode for the ret opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ret()");
    call_abs((void *) xlr8_load_jit_link);
    emit32le(mv_rr(HW_SCR_VAL, 10));
    reload_shadows();
    emit32le(jalr(HW_SCR_VAL));
}


void riscv64::emit_ppc()
// ----------------------------------------------------------------------------
//   Emit bytecode for the ppc opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ppc()");
    push_pc();
}


void riscv64::emit_pc(byte d)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pc opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pc(%u)", d);
    load_pc(d);
}


void riscv64::emit_pca(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pca opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pca(%u, %lld)", d, b);
    load_pca(d, b);
}


void riscv64::emit_enter(byte d)
// ----------------------------------------------------------------------------
//   Emit bytecode for the enter opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_enter(%u)", d);
    spill_frame();
    emit32le(mv_rr(10, HW_FLAGS));
    rtcall("xlr8_set_flags");
    rtcall("xlr8_enter", d);
    reload_frame_reg();
    reload_shadows();
}


void riscv64::emit_leave()
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


void riscv64::emit_get(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_get(%u, %u)", d, s);
    get_arg(d, s);
}


void riscv64::emit_set(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_set(%u, %u)", d, s);
    set_arg(d, s);
}


void riscv64::emit_push(byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_push(%u)", s);
    push_local(s);
}


void riscv64::emit_pop(byte d)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pop(%u)", d);
    pop_local(d);
}


void riscv64::emit_sys(tok_t t, byte_p args, byte_p next_pc)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sys opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sys(%llu, %p, %p)", t, args, next_pc);
    spill_arg_regs = true;
    spill_frame();
    load_imm64(10, uint64_t(t));
    load_imm64(11, uint64_t(uintptr_t(args)));
    call_abs((void *) xlr8_sys);
    reload_shadows();
    load_imm64(HW_SCR_VAL, uint64_t(uintptr_t(next_pc)));
    emit32le(mv_rr(10, HW_SCR_VAL));
    rtcall("xlr8_after_sys");
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
    code().patch32(bne_at, bne_imm(10, 0, int(br_at) - int(bne_at)));
    code().patch32(skip_at, j_imm(int(br_at + 4) - int(skip_at)));
}


void riscv64::emit_sto(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sto opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sto(%u, %u)", d, s);
    store_global(d, s);
}


void riscv64::emit_rcl(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_rcl(%u, %u)", d, s);
    load_global(d, s);
}


void riscv64::emit_dbra(byte d, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dbra(%u, %lld)", d, b);
    dbra_local(d, b);
}


void riscv64::emit_bra(broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bra opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bra(%lld)", b);
    bra_offset(b);
}


void riscv64::emit_btrue(byte p, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the btrue opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_btrue(%u, %lld)", p, b);
    bra_pred(p, b, true);
}


void riscv64::emit_bfalse(byte p, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bfalse opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bfalse(%u, %lld)", p, b);
    bra_pred(p, b, false);
}


void riscv64::emit_beq(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the beq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_beq(%u, %lld)", s, b);
    bra_zero(s, b, true);
}


void riscv64::emit_bne(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bne(%u, %lld)", s, b);
    bra_zero(s, b, false);
}


void riscv64::emit_blt(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the blt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_blt(%u, %lld)", s, b);
    bra_signed(s, b, 0xb);
}


void riscv64::emit_bgt(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bgt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bgt(%u, %lld)", s, b);
    bra_signed(s, b, 0xc);
}


void riscv64::emit_ble(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ble opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ble(%u, %lld)", s, b);
    bra_signed(s, b, 0xd);
}


void riscv64::emit_bge(byte s, broff_t b)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bge opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bge(%u, %lld)", s, b);
    bra_signed(s, b, 0xa);
}


void riscv64::emit_switch(byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the switch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_switch(%u)", s);
    rtcall("xlr8_switch", s);
}


void riscv64::emit_dispatch(byte s, byte_p table_pc)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dispatch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dispatch(%u, %p)", s, table_pc);
    spill_frame();
    load_imm64(10, uint64_t(s));
    load_imm64(11, uint64_t(uintptr_t(table_pc)));
    rtcall("xlr8_dispatch");
    br_via_ip();
}


void riscv64::emit_true(byte p)
// ----------------------------------------------------------------------------
//   Emit bytecode for the true opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_true(%u)", p);
    set_pred(p, true);
}


void riscv64::emit_false(byte p)
// ----------------------------------------------------------------------------
//   Emit bytecode for the false opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_false(%u)", p);
    set_pred(p, false);
}


void riscv64::emit_cmpeq(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeq(%u, %u, %u)", p, d, s);
    cmp_local(p, d, s, "eq");
}


void riscv64::emit_cmpne(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpne(%u, %u, %u)", p, d, s);
    cmp_local(p, d, s, "ne");
}


void riscv64::emit_cmplt(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplt(%u, %u, %u)", p, d, s);
    cmp_local(p, d, s, "lt");
}


void riscv64::emit_cmple(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmple opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmple(%u, %u, %u)", p, d, s);
    cmp_local(p, d, s, "le");
}


void riscv64::emit_cmpult(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpult opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpult(%u, %u, %u)", p, d, s);
    cmp_local(p, d, s, "ltu");
}


void riscv64::emit_cmpule(byte p, byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpule opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpule(%u, %u, %u)", p, d, s);
    cmp_local(p, d, s, "leu");
}


void riscv64::emit_cmpeqi(byte p, byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeqi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeqi(%u, %u, %lld)", p, d, v);
    cmp_imm(p, d, v, "eq");
}


void riscv64::emit_cmpnei(byte p, byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpnei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpnei(%u, %u, %lld)", p, d, v);
    cmp_imm(p, d, v, "ne");
}


void riscv64::emit_cmplti(byte p, byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplti opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplti(%u, %u, %lld)", p, d, v);
    cmp_imm(p, d, v, "lt");
}


void riscv64::emit_cmplei(byte p, byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplei(%u, %u, %lld)", p, d, v);
    cmp_imm(p, d, v, "le");
}


void riscv64::emit_cmpequ(byte p, byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpequ opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpequ(%u, %u, %llu)", p, d, v);
    cmp_uimm(p, d, v, "eq");
}


void riscv64::emit_cmpneu(byte p, byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpneu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpneu(%u, %u, %llu)", p, d, v);
    cmp_uimm(p, d, v, "ne");
}


void riscv64::emit_cmpltu(byte p, byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpltu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpltu(%u, %u, %llu)", p, d, v);
    cmp_uimm(p, d, v, "ltu");
}


void riscv64::emit_cmpleu(byte p, byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpleu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpleu(%u, %u, %llu)", p, d, v);
    cmp_uimm(p, d, v, "leu");
}


void riscv64::emit_pushf()
// ----------------------------------------------------------------------------
//   Emit bytecode for the pushf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pushf()");
    push_flags();
}


void riscv64::emit_popf()
// ----------------------------------------------------------------------------
//   Emit bytecode for the popf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_popf()");
    pop_flags();
}


void riscv64::emit_add(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the add opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_add(%u, %u)", d, s);
    binop_local(d, s, "add");
}


void riscv64::emit_sub(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sub(%u, %u)", d, s);
    binop_local(d, s, "sub");
}


void riscv64::emit_mul(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mul(%u, %u)", d, s);
    binop_local(d, s, "imul");
}


void riscv64::emit_umul(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umul(%u, %u)", d, s);
    binop_local(d, s, "imul");
}


void riscv64::emit_div(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the div opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_div(%u, %u)", d, s);
    binop_local(d, s, "idiv");
}


void riscv64::emit_udiv(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the udiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_udiv(%u, %u)", d, s);
    binop_local(d, s, "div");
}


void riscv64::emit_mod(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mod(%u, %u)", d, s);
    binop_local(d, s, "idiv_mod");
}


void riscv64::emit_umod(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umod(%u, %u)", d, s);
    binop_local(d, s, "div_mod");
}


void riscv64::emit_and(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the and opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_and(%u, %u)", d, s);
    binop_local(d, s, "and");
}


void riscv64::emit_or(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the or opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_or(%u, %u)", d, s);
    binop_local(d, s, "or");
}


void riscv64::emit_xor(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xor opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xor(%u, %u)", d, s);
    binop_local(d, s, "xor");
}


void riscv64::emit_shl(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shl(%u, %u)", d, s);
    binop_local(d, s, "shl");
}


void riscv64::emit_shr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shr(%u, %u)", d, s);
    binop_local(d, s, "sar");
}


void riscv64::emit_ushr(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushr(%u, %u)", d, s);
    binop_local(d, s, "shr");
}


void riscv64::emit_neg(byte d)
// ----------------------------------------------------------------------------
//   Emit bytecode for the neg opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_neg(%u)", d);
    neg_local(d);
}


void riscv64::emit_not(byte d)
// ----------------------------------------------------------------------------
//   Emit bytecode for the not opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_not(%u)", d);
    not_local(d);
}


void riscv64::emit_incr(byte d)
// ----------------------------------------------------------------------------
//   Emit bytecode for the incr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_incr(%u)", d);
    incr_local(d);
}


void riscv64::emit_decr(byte d)
// ----------------------------------------------------------------------------
//   Emit bytecode for the decr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_decr(%u)", d);
    decr_local(d);
}


void riscv64::emit_addi(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addi(%u, %lld)", d, v);
    binop_imm(d, v, "add");
}


void riscv64::emit_addu(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addu(%u, %llu)", d, v);
    binop_imm(d, v, "add");
}


void riscv64::emit_subi(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subi(%u, %lld)", d, v);
    binop_imm(d, v, "sub");
}


void riscv64::emit_subu(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subu(%u, %llu)", d, v);
    binop_imm(d, v, "sub");
}


void riscv64::emit_muli(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the muli opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_muli(%u, %lld)", d, v);
    binop_imm(d, v, "imul");
}


void riscv64::emit_mulu(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mulu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mulu(%u, %llu)", d, v);
    binop_imm(d, v, "imul");
}


void riscv64::emit_divi(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divi(%u, %lld)", d, v);
    binop_imm(d, v, "idiv_imm");
}


void riscv64::emit_divu(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divu(%u, %llu)", d, v);
    binop_imm(d, v, "div_imm");
}


void riscv64::emit_modi(byte d, value_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modi(%u, %lld)", d, v);
    binop_imm(d, v, "idiv_mod_imm");
}


void riscv64::emit_modu(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modu(%u, %llu)", d, v);
    binop_imm(d, v, "div_mod_imm");
}


void riscv64::emit_andu(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the andu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_andu(%u, %llu)", d, v);
    binop_imm(d, v, "and");
}


void riscv64::emit_oru(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the oru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_oru(%u, %llu)", d, v);
    binop_imm(d, v, "or");
}


void riscv64::emit_xoru(byte d, uvalue_t v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xoru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xoru(%u, %llu)", d, v);
    binop_imm(d, v, "xor");
}


void riscv64::emit_shlu(byte d, byte v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shlu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shlu(%u, %u)", d, v);
    binop_imm(d, v, "shl_imm");
}


void riscv64::emit_shru(byte d, byte v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shru(%u, %u)", d, v);
    binop_imm(d, v, "sar_imm");
}


void riscv64::emit_ushru(byte d, byte v)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushru(%u, %u)", d, v);
    binop_imm(d, v, "shr_imm");
}


void riscv64::emit_frame(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the frame opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_frame(%u, %u)", d, s);
    frame_addr(d, s);
}


void riscv64::emit_st8(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8(%u, %u)", d, s);
    store_mem(d, s, 8);
}


void riscv64::emit_st4(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4(%u, %u)", d, s);
    store_mem(d, s, 4);
}


void riscv64::emit_st2(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2(%u, %u)", d, s);
    store_mem(d, s, 2);
}


void riscv64::emit_st1(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1(%u, %u)", d, s);
    store_mem(d, s, 1);
}


void riscv64::emit_ld8(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8(%u, %u)", d, s);
    load_mem(d, s, 8);
}


void riscv64::emit_ld4(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4(%u, %u)", d, s);
    load_mem(d, s, 4);
}


void riscv64::emit_ld2(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2(%u, %u)", d, s);
    load_mem(d, s, 2);
}


void riscv64::emit_ld1(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1(%u, %u)", d, s);
    load_mem(d, s, 1);
}


void riscv64::emit_st8o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8o(%u, %u, %lld)", d, s, o);
    store_mem_off(d, s, o, 8);
}


void riscv64::emit_st4o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4o(%u, %u, %lld)", d, s, o);
    store_mem_off(d, s, o, 4);
}


void riscv64::emit_st2o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2o(%u, %u, %lld)", d, s, o);
    store_mem_off(d, s, o, 2);
}


void riscv64::emit_st1o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1o(%u, %u, %lld)", d, s, o);
    store_mem_off(d, s, o, 1);
}


void riscv64::emit_ld8o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8o(%u, %u, %lld)", d, s, o);
    load_mem_off(d, s, o, 8);
}


void riscv64::emit_ld4o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4o(%u, %u, %lld)", d, s, o);
    load_mem_off(d, s, o, 4);
}


void riscv64::emit_ld2o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2o(%u, %u, %lld)", d, s, o);
    load_mem_off(d, s, o, 2);
}


void riscv64::emit_ld1o(byte d, byte s, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1o(%u, %u, %lld)", d, s, o);
    load_mem_off(d, s, o, 1);
}


void riscv64::emit_lds(byte d, byte s, byte w)
// ----------------------------------------------------------------------------
//   Emit bytecode for the lds opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_lds(%u, %u, %u)", d, s, w);
    load_signext(d, s, w);
}


void riscv64::emit_ldos(byte d, byte s, byte w, value_t o)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ldos opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ldos(%u, %u, %u, %lld)", d, s, w, o);
    load_signext_off(d, s, w, o);
}


void riscv64::emit_sext(byte d, byte w)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sext(%u, %u)", d, w);
    sext_local(d, w);
}


void riscv64::emit_zext(byte d, byte w)
// ----------------------------------------------------------------------------
//   Emit bytecode for the zext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_zext(%u, %u)", d, w);
    zext_local(d, w);
}


void riscv64::emit_fadd(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fadd(%u, %u)", d, s);
    binop_local(d, s, "fadd");
}


void riscv64::emit_fsub(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fsub(%u, %u)", d, s);
    binop_local(d, s, "fsub");
}


void riscv64::emit_fmul(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fmul(%u, %u)", d, s);
    binop_local(d, s, "fmul");
}


void riscv64::emit_fdiv(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fdiv(%u, %u)", d, s);
    binop_local(d, s, "fdiv");
}


void riscv64::emit_dadd(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dadd(%u, %u)", d, s);
    binop_local(d, s, "dadd");
}


void riscv64::emit_dsub(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dsub(%u, %u)", d, s);
    binop_local(d, s, "dsub");
}


void riscv64::emit_dmul(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dmul(%u, %u)", d, s);
    binop_local(d, s, "dmul");
}


void riscv64::emit_ddiv(byte d, byte s)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ddiv(%u, %u)", d, s);
    binop_local(d, s, "ddiv");
}
