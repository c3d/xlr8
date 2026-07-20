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


void riscv64::emit_null(emit_args &args)
// ----------------------------------------------------------------------------
//   Null opcode in inline data: skip during translation (no native code)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_null()");
}


void riscv64::emit_user(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit a user-defined opcode (128..255, or registered low opcodes)
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_user(%lld, %p)", args.op, &args.pc);
    byte_p user_args = args.pc;
    spill_frame();
    load_imm64(10, uint64_t(args.op));
    load_imm64(11, uint64_t(uintptr_t(user_args)));
    rtcall("xlr8_user_op");
    reload_shadows();
}


void riscv64::emit_undef(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the undef opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_undef(%llu)", args.tok);
    bind(args.tok);
}


void riscv64::emit_int(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the int opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_int(%u, %lld)", args.dst, args.kint);
    store_imm(args.dst, value_t(args.kint));
}


void riscv64::emit_nat(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the nat opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_nat(%u, %llu)", args.dst, args.knat);
    store_uimm(args.dst, args.knat);
}


void riscv64::emit_flt(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the flt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_flt(%u, %g)", args.dst, args.kflt);
    store_float(args.dst, args.kflt);
}


void riscv64::emit_dbl(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dbl(%u, %g)", args.dst, args.kdbl);
    store_double(args.dst, args.kdbl);
}


void riscv64::emit_txt(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the txt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_txt(%u, %llu)", args.dst, args.tok);
    store_imm(args.dst, value_t(args.tok));
}


void riscv64::emit_copy(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the copy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_copy(%u, %u)", args.dst, args.src);
    mov_local(args.dst, args.src);
}


void riscv64::emit_swap(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the swap opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_swap(%u, %u)", args.dst, args.src);
    swap_locals(args.dst, args.src);
}


void riscv64::emit_ccopy(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ccopy opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ccopy(%u, %u, %u)", args.pred, args.dst, args.src);
    cond_mov_local(args.pred, args.dst, args.src);
}


void riscv64::emit_select(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the select opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_select(%u, %u, %u, %u)", args.pred, args.dst, args.src, args.xtra);
    select_local(args.pred, args.dst, args.src, args.xtra);
}


void riscv64::emit_jmp(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the jmp opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_jmp(%llu)", args.tok);
    jmp_tok(args.tok);
}


void riscv64::emit_call(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the call opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_call(%llu)", args.tok);
    call_tok(args.tok);
}


void riscv64::emit_ijmp(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ijmp opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ijmp(%u)", args.src);
    ijmp_local(args.src);
}


void riscv64::emit_icall(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the icall opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_icall(%u)", args.src);
    icall_local(args.src);
}


void riscv64::emit_ret(emit_args &args)
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


void riscv64::emit_ppc(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ppc opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ppc()");
    push_pc();
}


void riscv64::emit_pc(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pc opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pc(%u)", args.dst);
    load_pc(args.dst);
}


void riscv64::emit_pca(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pca opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pca(%u, %lld)", args.dst, args.broff);
    load_pca(args.dst, args.broff);
}


void riscv64::emit_enter(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the enter opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_enter(%u)", args.dst);
    spill_frame();
    emit32le(mv_rr(10, HW_FLAGS));
    rtcall("xlr8_set_flags");
    rtcall("xlr8_enter", args.dst);
    reload_frame_reg();
    reload_shadows();
}


void riscv64::emit_leave(emit_args &args)
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


void riscv64::emit_get(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the get opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_get(%u, %u)", args.dst, args.src);
    get_arg(args.dst, args.src);
}


void riscv64::emit_set(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the set opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_set(%u, %u)", args.dst, args.src);
    set_arg(args.dst, args.src);
}


void riscv64::emit_push(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the push opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_push(%u)", args.src);
    push_local(args.src);
}


void riscv64::emit_pop(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pop opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pop(%u)", args.dst);
    pop_local(args.dst);
}


void riscv64::emit_sys(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sys opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sys(%llu, %p, %p)", args.tok, args.pc, args.pc);
    spill_arg_regs = true;
    spill_frame();
    emit32le(mv_rr(10, HW_FLAGS));
    rtcall("xlr8_set_flags");
    load_imm64(10, uint64_t(args.tok));
    load_imm64(11, uint64_t(uintptr_t(args.pc)));
    call_abs((void *) xlr8_sys);
    // Linux helpers may move rt.frame (enter/leave/alloca); refresh s0 like
    // arm64'args.src post-sys xlr8_get_frame path so reload_shadows is not stale.
    rtcall("xlr8_get_frame");
    emit32le(reload_frame());
    // get_flags clobbers a0-a7 (shadow locals 0-7); reload shadows after it.
    reload_flags_reg();
    reload_shadows();
    load_imm64(HW_SCR_VAL, uint64_t(uintptr_t(args.pc)));
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


void riscv64::emit_sto(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sto opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sto(%u, %u)", args.dst, args.src);
    store_global(args.dst, args.src);
}


void riscv64::emit_rcl(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the rcl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_rcl(%u, %u)", args.dst, args.src);
    load_global(args.dst, args.src);
}


void riscv64::emit_dbra(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dbra opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dbra(%u, %lld)", args.dst, args.broff);
    dbra_local(args.dst, args.broff);
}


void riscv64::emit_bra(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bra opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bra(%lld)", args.broff);
    bra_offset(args.broff);
}


void riscv64::emit_btrue(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the btrue opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_btrue(%u, %lld)", args.pred, args.broff);
    bra_pred(args.pred, args.broff, true);
}


void riscv64::emit_bfalse(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bfalse opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bfalse(%u, %lld)", args.pred, args.broff);
    bra_pred(args.pred, args.broff, false);
}


void riscv64::emit_beq(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the beq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_beq(%u, %lld)", args.src, args.broff);
    bra_zero(args.src, args.broff, true);
}


void riscv64::emit_bne(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bne(%u, %lld)", args.src, args.broff);
    bra_zero(args.src, args.broff, false);
}


void riscv64::emit_blt(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the blt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_blt(%u, %lld)", args.src, args.broff);
    bra_signed(args.src, args.broff, 0xb);
}


void riscv64::emit_bgt(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bgt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bgt(%u, %lld)", args.src, args.broff);
    bra_signed(args.src, args.broff, 0xc);
}


void riscv64::emit_ble(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ble opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ble(%u, %lld)", args.src, args.broff);
    bra_signed(args.src, args.broff, 0xd);
}


void riscv64::emit_bge(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the bge opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_bge(%u, %lld)", args.src, args.broff);
    bra_signed(args.src, args.broff, 0xa);
}


void riscv64::emit_switch(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the switch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_switch(%u)", args.src);
    rtcall("xlr8_switch", args.src);
}


void riscv64::emit_dispatch(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dispatch opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dispatch(%u, %p)", args.src, args.pc);
    spill_frame();
    load_imm64(10, uint64_t(args.src));
    load_imm64(11, uint64_t(uintptr_t(args.pc)));
    rtcall("xlr8_dispatch");
    br_via_ip();
}


void riscv64::emit_true(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the true opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_true(%u)", args.pred);
    set_pred(args.pred, true);
}


void riscv64::emit_false(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the false opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_false(%u)", args.pred);
    set_pred(args.pred, false);
}


void riscv64::emit_cmpeq(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeq opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeq(%u, %u, %u)", args.pred, args.dst, args.src);
    cmp_local(args.pred, args.dst, args.src, "eq");
}


void riscv64::emit_cmpne(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpne opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpne(%u, %u, %u)", args.pred, args.dst, args.src);
    cmp_local(args.pred, args.dst, args.src, "ne");
}


void riscv64::emit_cmplt(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplt opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplt(%u, %u, %u)", args.pred, args.dst, args.src);
    cmp_local(args.pred, args.dst, args.src, "lt");
}


void riscv64::emit_cmple(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmple opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmple(%u, %u, %u)", args.pred, args.dst, args.src);
    cmp_local(args.pred, args.dst, args.src, "le");
}


void riscv64::emit_cmpult(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpult opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpult(%u, %u, %u)", args.pred, args.dst, args.src);
    cmp_local(args.pred, args.dst, args.src, "ltu");
}


void riscv64::emit_cmpule(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpule opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpule(%u, %u, %u)", args.pred, args.dst, args.src);
    cmp_local(args.pred, args.dst, args.src, "leu");
}


void riscv64::emit_cmpeqi(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpeqi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpeqi(%u, %u, %lld)", args.pred, args.dst, args.kint);
    cmp_imm(args.pred, args.dst, args.kint, "eq");
}


void riscv64::emit_cmpnei(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpnei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpnei(%u, %u, %lld)", args.pred, args.dst, args.kint);
    cmp_imm(args.pred, args.dst, args.kint, "ne");
}


void riscv64::emit_cmplti(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplti opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplti(%u, %u, %lld)", args.pred, args.dst, args.kint);
    cmp_imm(args.pred, args.dst, args.kint, "lt");
}


void riscv64::emit_cmplei(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmplei opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmplei(%u, %u, %lld)", args.pred, args.dst, args.kint);
    cmp_imm(args.pred, args.dst, args.kint, "le");
}


void riscv64::emit_cmpequ(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpequ opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpequ(%u, %u, %llu)", args.pred, args.dst, args.knat);
    cmp_uimm(args.pred, args.dst, args.knat, "eq");
}


void riscv64::emit_cmpneu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpneu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpneu(%u, %u, %llu)", args.pred, args.dst, args.knat);
    cmp_uimm(args.pred, args.dst, args.knat, "ne");
}


void riscv64::emit_cmpltu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpltu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpltu(%u, %u, %llu)", args.pred, args.dst, args.knat);
    cmp_uimm(args.pred, args.dst, args.knat, "ltu");
}


void riscv64::emit_cmpleu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the cmpleu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_cmpleu(%u, %u, %llu)", args.pred, args.dst, args.knat);
    cmp_uimm(args.pred, args.dst, args.knat, "leu");
}


void riscv64::emit_pushf(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the pushf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_pushf()");
    push_flags();
}


void riscv64::emit_popf(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the popf opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_popf()");
    pop_flags();
}


void riscv64::emit_add(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the add opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_add(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "add");
}


void riscv64::emit_sub(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sub(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "sub");
}


void riscv64::emit_mul(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mul(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "imul");
}


void riscv64::emit_umul(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umul(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "imul");
}


void riscv64::emit_div(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the div opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_div(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "idiv");
}


void riscv64::emit_udiv(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the udiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_udiv(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "div");
}


void riscv64::emit_mod(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mod(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "idiv_mod");
}


void riscv64::emit_umod(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the umod opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_umod(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "div_mod");
}


void riscv64::emit_and(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the and opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_and(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "and");
}


void riscv64::emit_or(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the or opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_or(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "or");
}


void riscv64::emit_xor(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xor opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xor(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "xor");
}


void riscv64::emit_shl(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shl opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shl(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "shl");
}


void riscv64::emit_shr(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shr(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "sar");
}


void riscv64::emit_ushr(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushr(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "shr");
}


void riscv64::emit_neg(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the neg opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_neg(%u)", args.dst);
    neg_local(args.dst);
}


void riscv64::emit_not(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the not opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_not(%u)", args.dst);
    not_local(args.dst);
}


void riscv64::emit_incr(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the incr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_incr(%u)", args.dst);
    incr_local(args.dst);
}


void riscv64::emit_decr(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the decr opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_decr(%u)", args.dst);
    decr_local(args.dst);
}


void riscv64::emit_addi(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addi(%u, %lld)", args.dst, args.kint);
    binop_imm(args.dst, args.kint, "add");
}


void riscv64::emit_addu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the addu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_addu(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "add");
}


void riscv64::emit_subi(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subi(%u, %lld)", args.dst, args.kint);
    binop_imm(args.dst, args.kint, "sub");
}


void riscv64::emit_subu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the subu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_subu(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "sub");
}


void riscv64::emit_muli(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the muli opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_muli(%u, %lld)", args.dst, args.kint);
    binop_imm(args.dst, args.kint, "imul");
}


void riscv64::emit_mulu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the mulu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_mulu(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "imul");
}


void riscv64::emit_divi(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divi(%u, %lld)", args.dst, args.kint);
    binop_imm(args.dst, args.kint, "idiv_imm");
}


void riscv64::emit_divu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the divu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_divu(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "div_imm");
}


void riscv64::emit_modi(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modi opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modi(%u, %lld)", args.dst, args.kint);
    binop_imm(args.dst, args.kint, "idiv_mod_imm");
}


void riscv64::emit_modu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the modu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_modu(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "div_mod_imm");
}


void riscv64::emit_andu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the andu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_andu(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "and");
}


void riscv64::emit_oru(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the oru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_oru(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "or");
}


void riscv64::emit_xoru(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the xoru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_xoru(%u, %llu)", args.dst, args.knat);
    binop_imm(args.dst, args.knat, "xor");
}


void riscv64::emit_shlu(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shlu opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shlu(%u, %u)", args.dst, args.kshift);
    binop_imm(args.dst, args.kshift, "shl_imm");
}


void riscv64::emit_shru(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the shru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_shru(%u, %u)", args.dst, args.kshift);
    binop_imm(args.dst, args.kshift, "sar_imm");
}


void riscv64::emit_ushru(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ushru opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ushru(%u, %u)", args.dst, args.kshift);
    binop_imm(args.dst, args.kshift, "shr_imm");
}


void riscv64::emit_frame(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the frame opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_frame(%u, %u)", args.dst, args.src);
    frame_addr(args.dst, args.src);
}


void riscv64::emit_st8(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8(%u, %u)", args.dst, args.src);
    store_mem(args.dst, args.src, 8);
}


void riscv64::emit_st4(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4(%u, %u)", args.dst, args.src);
    store_mem(args.dst, args.src, 4);
}


void riscv64::emit_st2(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2(%u, %u)", args.dst, args.src);
    store_mem(args.dst, args.src, 2);
}


void riscv64::emit_st1(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1(%u, %u)", args.dst, args.src);
    store_mem(args.dst, args.src, 1);
}


void riscv64::emit_ld8(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8(%u, %u)", args.dst, args.src);
    load_mem(args.dst, args.src, 8);
}


void riscv64::emit_ld4(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4(%u, %u)", args.dst, args.src);
    load_mem(args.dst, args.src, 4);
}


void riscv64::emit_ld2(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2(%u, %u)", args.dst, args.src);
    load_mem(args.dst, args.src, 2);
}


void riscv64::emit_ld1(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1 opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1(%u, %u)", args.dst, args.src);
    load_mem(args.dst, args.src, 1);
}


void riscv64::emit_st8o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st8o(%u, %u, %lld)", args.dst, args.src, args.kint);
    store_mem_off(args.dst, args.src, args.kint, 8);
}


void riscv64::emit_st4o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st4o(%u, %u, %lld)", args.dst, args.src, args.kint);
    store_mem_off(args.dst, args.src, args.kint, 4);
}


void riscv64::emit_st2o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st2o(%u, %u, %lld)", args.dst, args.src, args.kint);
    store_mem_off(args.dst, args.src, args.kint, 2);
}


void riscv64::emit_st1o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the st1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_st1o(%u, %u, %lld)", args.dst, args.src, args.kint);
    store_mem_off(args.dst, args.src, args.kint, 1);
}


void riscv64::emit_ld8o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld8o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld8o(%u, %u, %lld)", args.dst, args.src, args.kint);
    load_mem_off(args.dst, args.src, args.kint, 8);
}


void riscv64::emit_ld4o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld4o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld4o(%u, %u, %lld)", args.dst, args.src, args.kint);
    load_mem_off(args.dst, args.src, args.kint, 4);
}


void riscv64::emit_ld2o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld2o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld2o(%u, %u, %lld)", args.dst, args.src, args.kint);
    load_mem_off(args.dst, args.src, args.kint, 2);
}


void riscv64::emit_ld1o(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ld1o opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ld1o(%u, %u, %lld)", args.dst, args.src, args.kint);
    load_mem_off(args.dst, args.src, args.kint, 1);
}


void riscv64::emit_lds(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the lds opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_lds(%u, %u, %u)", args.dst, args.src, args.kshift);
    load_signext(args.dst, args.src, args.kshift);
}


void riscv64::emit_ldos(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ldos opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ldos(%u, %u, %u, %lld)", args.dst, args.src, args.kshift, args.kint);
    load_signext_off(args.dst, args.src, args.kshift, args.kint);
}


void riscv64::emit_sext(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the sext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_sext(%u, %u)", args.dst, args.kshift);
    sext_local(args.dst, args.kshift);
}


void riscv64::emit_zext(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the zext opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_zext(%u, %u)", args.dst, args.kshift);
    zext_local(args.dst, args.kshift);
}


void riscv64::emit_fadd(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fadd(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "fadd");
}


void riscv64::emit_fsub(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fsub(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "fsub");
}


void riscv64::emit_fmul(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fmul(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "fmul");
}


void riscv64::emit_fdiv(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the fdiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_fdiv(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "fdiv");
}


void riscv64::emit_dadd(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dadd opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dadd(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "dadd");
}


void riscv64::emit_dsub(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dsub opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dsub(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "dsub");
}


void riscv64::emit_dmul(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the dmul opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_dmul(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "dmul");
}


void riscv64::emit_ddiv(emit_args &args)
// ----------------------------------------------------------------------------
//   Emit bytecode for the ddiv opcode
// ----------------------------------------------------------------------------
{
    record(riscv64_ops, "emit_ddiv(%u, %u)", args.dst, args.src);
    binop_local(args.dst, args.src, "ddiv");
}
