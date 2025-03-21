//===-- VCSIRInstrInfo.td - Target Description for RISC-V --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VCSIR specific DAG Nodes.
//===----------------------------------------------------------------------===//

// Target-independent type requirements, but with target-specific formats.
def SDT_CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_CallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

// Target-dependent type requirements.
def SDT_VCSIRCall : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;
def SDT_VCSIRSelectCC : SDTypeProfile<1, 5,
                                      [SDTCisSameAs<1, 2>, SDTCisVT<3, OtherVT>,
                                       SDTCisSameAs<0, 4>, SDTCisSameAs<4, 5>]>;
def SDT_VCSIRBrCC
    : SDTypeProfile<
          0,
          4, [SDTCisSameAs<0, 1>, SDTCisVT<2, OtherVT>, SDTCisVT<3, OtherVT>]>;

def SDT_VCSIRReadCounterWide
    : SDTypeProfile<
          2,
          2, [SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisInt<2>, SDTCisInt<3>]>;
def SDT_VCSIRIntUnaryOpW
    : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisVT<0, i64>]>;
def SDT_VCSIRIntBinOpW
    : SDTypeProfile<
          1, 2, [SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisVT<0, i64>]>;
def SDT_VCSIRIntShiftDOpW
    : SDTypeProfile<1, 3,
                    [SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisVT<0, i64>,
                     SDTCisVT<3, i64>]>;

// Target-independent nodes, but with target-specific formats.
def callseq_start : SDNode<"ISD::CALLSEQ_START",
                           SDT_CallSeqStart, [SDNPHasChain, SDNPOutGlue]>;
def callseq_end
    : SDNode<"ISD::CALLSEQ_END",
             SDT_CallSeqEnd, [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// Target-dependent nodes.
def vcsir_call
    : SDNode<"VCSIRISD::CALL", SDT_VCSIRCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def vcsir_sw_guarded_call
    : SDNode<"VCSIRISD::SW_GUARDED_CALL", SDT_VCSIRCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def vcsir_ret_glue
    : SDNode<"VCSIRISD::RET_GLUE",
             SDTNone, [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def vcsir_selectcc : SDNode<"VCSIRISD::SELECT_CC", SDT_VCSIRSelectCC>;
def vcsir_brcc : SDNode<"VCSIRISD::BR_CC", SDT_VCSIRBrCC, [SDNPHasChain]>;
def vcsir_tail
    : SDNode<"VCSIRISD::TAIL", SDT_VCSIRCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def vcsir_sw_guarded_tail
    : SDNode<"VCSIRISD::SW_GUARDED_TAIL", SDT_VCSIRCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def vcsir_sw_guarded_brind
    : SDNode<"VCSIRISD::SW_GUARDED_BRIND", SDTBrind, [SDNPHasChain]>;
def vcsir_sllw : SDNode<"VCSIRISD::SLLW", SDT_VCSIRIntBinOpW>;
def vcsir_sraw : SDNode<"VCSIRISD::SRAW", SDT_VCSIRIntBinOpW>;
def vcsir_srlw : SDNode<"VCSIRISD::SRLW", SDT_VCSIRIntBinOpW>;

def vcsir_read_counter_wide
    : SDNode<"VCSIRISD::READ_COUNTER_WIDE",
             SDT_VCSIRReadCounterWide, [SDNPHasChain, SDNPSideEffect]>;

def vcsir_add_lo : SDNode<"VCSIRISD::ADD_LO", SDTIntBinOp>;
def vcsir_hi : SDNode<"VCSIRISD::HI", SDTIntUnaryOp>;
def vcsir_lla : SDNode<"VCSIRISD::LLA", SDTIntUnaryOp>;
def vcsir_add_tprel
    : SDNode<"VCSIRISD::ADD_TPREL",
             SDTypeProfile<1, 3,
                           [SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>,
                            SDTCisSameAs<0, 3>, SDTCisInt<0>]>>;

def vcsir_probed_alloca
    : SDNode<"VCSIRISD::PROBED_ALLOCA",
             SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisVT<0, i32>]>,
             [SDNPHasChain, SDNPMayStore]>;

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

class ImmXLenAsmOperand<string prefix, string suffix = ""> : AsmOperandClass {
  let Name = prefix#"ImmXLen"#suffix;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", Name);
}

class ImmAsmOperand<string prefix, int width, string suffix> : AsmOperandClass {
  let Name = prefix#"Imm"#width#suffix;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", Name);
}

def ImmZeroAsmOperand : AsmOperandClass {
  let Name = "ImmZero";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", Name);
  let DiagnosticString = "immediate must be zero";
}

// A parse method for (${gpr}) or 0(${gpr}), where the 0 is be silently ignored.
def ZeroOffsetMemOpOperand : AsmOperandClass {
  let Name = "ZeroOffsetMemOpOperand";
  let RenderMethod = "addRegOperands";
  let PredicateMethod = "isGPR";
  let ParserMethod = "parseZeroOffsetMemOp";
}

class MemOperand<RegisterClass regClass> : RegisterOperand<regClass> {
  let OperandType = "OPERAND_MEMORY";
}

def GPRMemZeroOffset : MemOperand<GPR> {
  let ParserMatchClass = ZeroOffsetMemOpOperand;
  let PrintMethod = "printZeroOffsetMemOp";
}

def GPRMem : MemOperand<GPR>;

def SPMem : MemOperand<SP>;

class SImmAsmOperand<int width, string suffix = "">
    : ImmAsmOperand<"S", width, suffix> {}

class UImmAsmOperand<int width, string suffix = "">
    : ImmAsmOperand<"U", width, suffix> {}

class VCSIROp<ValueType vt = i32> : Operand<vt> {
  let OperandNamespace = "VCSIROp";
}

class VCSIRUImmOp<int bitsNum> : VCSIROp {
  let ParserMatchClass = UImmAsmOperand<bitsNum>;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<"#bitsNum#">";
  let OperandType = "OPERAND_UIMM"#bitsNum;
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isUInt<}]#bitsNum#[{>(Imm);
  }];
}

class VCSIRUImmLeafOp<int bitsNum>
    : VCSIRUImmOp<bitsNum>, ImmLeaf<i32, "return isUInt<"#bitsNum#">(Imm);">;

class VCSIRSImmOp<int bitsNum> : VCSIROp {
  let ParserMatchClass = SImmAsmOperand<bitsNum>;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeSImmOperand<"#bitsNum#">";
  let OperandType = "OPERAND_SIMM"#bitsNum;
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    return isInt<}]#bitsNum#[{>(Imm);
  }];
}

class VCSIRSImmLeafOp<int bitsNum>
    : VCSIRSImmOp<bitsNum>, ImmLeaf<i32, "return isInt<"#bitsNum#">(Imm);">;

def UImmLog2XLenAsmOperand : AsmOperandClass {
  let Name = "UImmLog2XLen";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidUImmLog2XLen";
}

def uimmlog2xlen : VCSIROp, ImmLeaf<i32, [{
  if (Subtarget->is64Bit())
    return isUInt<6>(Imm);
  return isUInt<5>(Imm);
}]> {
  let ParserMatchClass = UImmLog2XLenAsmOperand;
  let DecoderMethod = "decodeUImmLog2XLenOperand";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    if (STI.getTargetTriple().isArch64Bit())
      return isUInt<6>(Imm);
    return isUInt<5>(Imm);
  }];
  let OperandType = "OPERAND_UIMMLOG2XLEN";
}

def uimm1 : VCSIRUImmLeafOp<1>;
def uimm2 : VCSIRUImmLeafOp<2>;
def uimm3 : VCSIRUImmOp<3>;
def uimm4 : VCSIRUImmLeafOp<4>;
def uimm5 : VCSIRUImmLeafOp<5>;
def uimm6 : VCSIRUImmLeafOp<6>;
def uimm7_opcode : VCSIRUImmOp<7> {}
def uimm7 : VCSIRUImmOp<7>;
def uimm8 : VCSIRUImmOp<8>;
def uimm16 : VCSIRUImmOp<16>;
def uimm32 : VCSIRUImmOp<32>;
def uimm48 : VCSIRUImmOp<48>;
def uimm64 : VCSIRUImmOp<64>;
def simm12 : VCSIRSImmLeafOp<12> {
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isInt<12>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

// A 12-bit signed immediate which cannot fit in 6-bit signed immediate,
// but even negative value fit in 12-bit.
def simm12_no6 : ImmLeaf<i32, [{
  return isInt<12>(Imm) && !isInt<6>(Imm) && isInt<12>(-Imm);}]>;

// A 13-bit signed immediate where the least significant bit is zero.
def simm13_lsb0 : Operand<OtherVT> {
  let ParserMatchClass = SImmAsmOperand<13, "Lsb0">;
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getImmOpValueAsr1";
  let DecoderMethod = "decodeSImmOperandAndLsl1<13>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isShiftedInt<12, 1>(Imm);
    return MCOp.isBareSymbolRef();
  }];
  let OperandType = "OPERAND_PCREL";
}

class UImm20OperandMaybeSym : VCSIRUImmOp<20> {
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isUInt<20>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

def uimm20_lui : UImm20OperandMaybeSym {
  let ParserMatchClass = UImmAsmOperand<20, "LUI">;
}
def uimm20_auipc : UImm20OperandMaybeSym {
  let ParserMatchClass = UImmAsmOperand<20, "AUIPC">;
}

def uimm20 : VCSIRUImmOp<20>;

def Simm21Lsb0JALAsmOperand : SImmAsmOperand<21, "Lsb0JAL"> {
  let ParserMethod = "parseJALOffset";
}

// A 21-bit signed immediate where the least significant bit is zero.
def simm21_lsb0_jal : Operand<OtherVT> {
  let ParserMatchClass = Simm21Lsb0JALAsmOperand;
  let PrintMethod = "printBranchOperand";
  let EncoderMethod = "getImmOpValueAsr1";
  let DecoderMethod = "decodeSImmOperandAndLsl1<21>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isShiftedInt<20, 1>(Imm);
    return MCOp.isBareSymbolRef();
  }];
  let OperandType = "OPERAND_PCREL";
}

def BareSymbol : AsmOperandClass {
  let Name = "BareSymbol";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidBareSymbol";
  let DiagnosticString = "operand must be a bare symbol name";
  let ParserMethod = "parseBareSymbol";
}

// A bare symbol.
def bare_symbol : Operand<i32> { let ParserMatchClass = BareSymbol; }

def CallSymbol : AsmOperandClass {
  let Name = "CallSymbol";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidCallSymbol";
  let DiagnosticString = "operand must be a bare symbol name";
  let ParserMethod = "parseCallSymbol";
}

// A bare symbol used in call/tail only.
def call_symbol : Operand<i32> { let ParserMatchClass = CallSymbol; }

def PseudoJumpSymbol : AsmOperandClass {
  let Name = "PseudoJumpSymbol";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidPseudoJumpSymbol";
  let DiagnosticString = "operand must be a valid jump target";
  let ParserMethod = "parsePseudoJumpSymbol";
}

// A bare symbol used for pseudo jumps only.
def pseudo_jump_symbol : Operand<i32> {
  let ParserMatchClass = PseudoJumpSymbol;
}

// A parameterized register class alternative to i32imm/i64imm from Target.td.
def ixlenimm : Operand<i32>;

// Condition code used by select and short forward branch pseudos.
def cond_code : VCSIROp { let OperandType = "OPERAND_COND_CODE"; }

def ixlenimm_li : Operand<i32> {
  let ParserMatchClass = ImmXLenAsmOperand<"", "LI">;
}

// Accepts subset of LI operands, used by LAImm and LLAImm
def ixlenimm_li_restricted : Operand<i32> {
  let ParserMatchClass = ImmXLenAsmOperand<"", "LI_Restricted">;
}

// Standalone (codegen-only) immleaf patterns.

// A 12-bit signed immediate plus one where the imm range will be -2047~2048.
def simm12_plus1
    : ImmLeaf<i32, [{return (isInt<12>(Imm) && Imm != -2048) || Imm == 2048;}]>;

// A 6-bit constant greater than 32.
def uimm6gt32 : ImmLeaf<i32, [{
  return isUInt<6>(Imm) && Imm > 32;
}]>;

// Addressing modes.
def AddrRegImm : ComplexPattern<iPTR, 2, "SelectAddrRegImm">;

// Return the negation of an immediate value.
def NegImm : SDNodeXForm<imm, [{
  return CurDAG->getSignedTargetConstant(-N->getSExtValue(), SDLoc(N),
                                         N->getValueType(0));
}]>;
def GINegImm : GICustomOperandRenderer<"renderNegImm">,
               GISDNodeXFormEquiv<NegImm>;

// Return an immediate value minus 32.
def ImmSub32 : SDNodeXForm<imm, [{
  return CurDAG->getSignedTargetConstant(N->getSExtValue() - 32, SDLoc(N),
                                         N->getValueType(0));
}]>;

// Return an immediate subtracted from XLen.
def ImmSubFromXLen : SDNodeXForm<imm, [{
  uint64_t XLen = Subtarget->getXLen();
  return CurDAG->getTargetConstant(XLen - N->getZExtValue(), SDLoc(N),
                                   N->getValueType(0));
}]>;
def GIImmSubFromXLen : GICustomOperandRenderer<"renderImmSubFromXLen">,
                       GISDNodeXFormEquiv<ImmSubFromXLen>;

// Return an immediate subtracted from 32.
def ImmSubFrom32 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(32 - N->getZExtValue(), SDLoc(N),
                                   N->getValueType(0));
}]>;
def GIImmSubFrom32 : GICustomOperandRenderer<"renderImmSubFrom32">,
                     GISDNodeXFormEquiv<ImmSubFrom32>;

// Check if (add r, imm) can be optimized to (ADDI (ADDI r, imm0), imm1),
// in which imm = imm0 + imm1 and both imm0 and imm1 are simm12. We make imm0
// as large as possible and imm1 as small as possible so that we might be able
// to use c.addi for the small immediate.
def AddiPair : ImmLeaf<i32, [{
  // The immediate operand must be in range [-4096,-2049] or [2048,4094].
  return (-4096 <= Imm && Imm <= -2049) || (2048 <= Imm && Imm <= 4094);
}]>;

// Return imm - (imm < 0 ? -2048 : 2047).
def AddiPairImmSmall : SDNodeXForm<imm, [{
  int64_t Imm = N->getSExtValue();
  int64_t Adj = N->getSExtValue() < 0 ? -2048 : 2047;
  return CurDAG->getSignedTargetConstant(Imm - Adj, SDLoc(N),
                                         N->getValueType(0));
}]>;
def GIAddiPairImmSmall : GICustomOperandRenderer<"renderAddiPairImmSmall">,
                         GISDNodeXFormEquiv<AddiPairImmSmall>;

// Return -2048 if immediate is negative or 2047 if positive. These are the
// largest simm12 values.
def AddiPairImmLarge : SDNodeXForm<imm, [{
  int64_t Imm = N->getSExtValue() < 0 ? -2048 : 2047;
  return CurDAG->getSignedTargetConstant(Imm, SDLoc(N), N->getValueType(0));
}]>;
def GIAddiPairImmLarge : GICustomOperandRenderer<"renderAddiPairImmLarge">,
                         GISDNodeXFormEquiv<AddiPairImmLarge>;

def TrailingZeros : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(llvm::countr_zero(N->getZExtValue()),
                                   SDLoc(N), N->getValueType(0));
}]>;
def GITrailingZeros : GICustomOperandRenderer<"renderTrailingZeros">,
                      GISDNodeXFormEquiv<TrailingZeros>;

def XLenSubTrailingOnes : SDNodeXForm<imm, [{
  uint64_t XLen = Subtarget->getXLen();
  uint64_t TrailingOnes = llvm::countr_one(N->getZExtValue());
  return CurDAG->getTargetConstant(XLen - TrailingOnes, SDLoc(N),
                                   N->getValueType(0));
}]>;
def GIXLenSubTrailingOnes
    : GICustomOperandRenderer<"renderXLenSubTrailingOnes">,
      GISDNodeXFormEquiv<XLenSubTrailingOnes>;

// Checks if this mask is a non-empty sequence of ones starting at the
// most/least significant bit with the remainder zero and exceeds simm32/simm12.
def LeadingOnesMask : ImmLeaf<i32, [{
  return !isInt<32>(Imm) && isMask_64(~Imm);
}],
                              TrailingZeros>;

def TrailingOnesMask : IntImmLeaf<i32, [{
  return !isInt<12>(Imm.getSExtValue()) && isMask_64(Imm.getZExtValue());
}],
                                  XLenSubTrailingOnes>;

// Similar to LeadingOnesMask, but only consider leading ones in the lower 32
// bits.
def LeadingOnesWMask : ImmLeaf<i32, [{
  // If the value is a uint32 but not an int32, it must have bit 31 set and
  // bits 63:32 cleared. After that we're looking for a shifted mask but not
  // an all ones mask.
  return !isInt<32>(Imm) && isUInt<32>(Imm) && isShiftedMask_64(Imm) &&
         Imm != UINT64_C(0xffffffff);
}],
                               TrailingZeros>;

//===----------------------------------------------------------------------===//
// Instruction Formats
//===----------------------------------------------------------------------===//

include "VCSIRInstrFormats.td"

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in class BranchCC_rri<bits<3> funct3, string opcodestr>
    : VCSIRInstB<funct3, OPC_BRANCH, (outs),
                 (ins GPR:$rs1, GPR:$rs2, simm13_lsb0:$imm12), opcodestr,
                 "$rs1, $rs2, $imm12"> {
  let isBranch = 1;
  let isTerminator = 1;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
  class Load_ri<bits<3> funct3, string opcodestr, DAGOperand rty = GPR>
      : VCSIRInstI<funct3, OPC_LOAD, (outs rty:$rd),
                   (ins GPRMem:$rs1, simm12:$imm12), opcodestr,
                   "$rd, ${imm12}(${rs1})">;

  class HLoad_r<bits<7> funct7, bits<5> funct5, string opcodestr>
      : VCSIRInstR<funct7, 0b100, OPC_SYSTEM, (outs GPR:$rd),
                   (ins GPRMemZeroOffset:$rs1), opcodestr, "$rd, $rs1"> {
    let rs2 = funct5;
  }
}

// Operands for stores are in the order srcreg, base, offset rather than
// reflecting the order these fields are specified in the instruction
// encoding.
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
  class Store_rri<bits<3> funct3, string opcodestr, DAGOperand rty = GPR>
      : VCSIRInstS<funct3, OPC_STORE, (outs),
                   (ins rty:$rs2, GPRMem:$rs1, simm12:$imm12), opcodestr,
                   "$rs2, ${imm12}(${rs1})">;

  class HStore_rr<bits<7> funct7, string opcodestr>
      : VCSIRInstR<funct7, 0b100, OPC_SYSTEM, (outs),
                   (ins GPR:$rs2, GPRMemZeroOffset:$rs1), opcodestr,
                   "$rs2, $rs1"> {
    let rd = 0;
  }
}

let hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in class ALU_ri<bits<3> funct3, string opcodestr>
    : VCSIRInstI<funct3, OPC_OP_IMM, (outs GPR:$rd),
                 (ins GPR:$rs1, simm12:$imm12), opcodestr, "$rd, $rs1, $imm12">;

let hasSideEffects = 0, mayLoad = 0,
    mayStore =
        0 in class Shift_ri<bits<5> imm11_7, bits<3> funct3, string opcodestr>
    : VCSIRInstIShift<imm11_7, funct3, OPC_OP_IMM, (outs GPR:$rd),
                      (ins GPR:$rs1, uimmlog2xlen:$shamt), opcodestr,
                      "$rd, $rs1, $shamt">;

let hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in class ALU_rr<bits<7> funct7, bits<3> funct3,
                                 string opcodestr, bit Commutable = 0>
    : VCSIRInstR<funct7, funct3, OPC_OP, (outs GPR:$rd),
                 (ins GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2"> {
  let isCommutable = Commutable;
}

let hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in class ALUW_rr<bits<7> funct7, bits<3> funct3,
                                  string opcodestr, bit Commutable = 0>
    : VCSIRInstR<funct7, funct3, OPC_OP_32, (outs GPR:$rd),
                 (ins GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2"> {
  let isCommutable = Commutable;
}

let hasSideEffects = 0, mayLoad = 0,
    mayStore =
        0 in class Unary_r<bits<12> imm12, bits<3> funct3, string opcodestr>
    : VCSIRInstIUnary<imm12, funct3, OPC_OP_IMM, (outs GPR:$rd), (ins GPR:$rs1),
                      opcodestr, "$rd, $rs1">;

let hasSideEffects = 0, mayLoad = 0,
    mayStore =
        0 in class UnaryW_r<bits<12> imm12, bits<3> funct3, string opcodestr>
    : VCSIRInstIUnary<imm12, funct3, OPC_OP_IMM_32, (outs GPR:$rd),
                      (ins GPR:$rs1), opcodestr, "$rd, $rs1">;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
  let isReMaterializable = 1, isAsCheapAsAMove = 1,
      IsSignExtendingOpW = 1 in def LUI
      : VCSIRInstU<OPC_LUI, (outs GPR:$rd), (ins uimm20_lui:$imm20), "lui",
                   "$rd, $imm20">;

  def AUIPC : VCSIRInstU<OPC_AUIPC, (outs GPR:$rd), (ins uimm20_auipc:$imm20),
                         "auipc", "$rd, $imm20">;

  def JAL : VCSIRInstJ<OPC_JAL, (outs GPR:$rd), (ins simm21_lsb0_jal:$imm20),
                       "jal", "$rd, $imm20">;

  def JALR : VCSIRInstI<0b000, OPC_JALR, (outs GPR:$rd),
                        (ins GPR:$rs1, simm12:$imm12), "jalr",
                        "$rd, ${imm12}(${rs1})">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

def BEQ : BranchCC_rri<0b000, "beq">;
def BNE : BranchCC_rri<0b001, "bne">;
def BLT : BranchCC_rri<0b100, "blt">;
def BGE : BranchCC_rri<0b101, "bge">;
def BLTU : BranchCC_rri<0b110, "bltu">;
def BGEU : BranchCC_rri<0b111, "bgeu">;

let IsSignExtendingOpW = 1 in {
  def LB : Load_ri<0b000, "lb">;
  def LH : Load_ri<0b001, "lh">;
  def LW : Load_ri<0b010, "lw">;
  def LBU : Load_ri<0b100, "lbu">;
  def LHU : Load_ri<0b101, "lhu">;
}

def SB : Store_rri<0b000, "sb">;
def SH : Store_rri<0b001, "sh">;
def SW : Store_rri<0b010, "sw">;

// ADDI isn't always rematerializable, but isReMaterializable will be used as
// a hint which is verified in isReallyTriviallyReMaterializable.
let isReMaterializable = 1,
    isAsCheapAsAMove = 1 in def ADDI : ALU_ri<0b000, "addi">;

let IsSignExtendingOpW = 1 in {
  def SLTI : ALU_ri<0b010, "slti">;
  def SLTIU : ALU_ri<0b011, "sltiu">;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
  def XORI : ALU_ri<0b100, "xori">;
  def ORI : ALU_ri<0b110, "ori">;
}

def ANDI : ALU_ri<0b111, "andi">;

def SLLI : Shift_ri<0b00000, 0b001, "slli">;
def SRLI : Shift_ri<0b00000, 0b101, "srli">;
def SRAI : Shift_ri<0b01000, 0b101, "srai">;

def ADD : ALU_rr<0b0000000, 0b000, "add", Commutable = 1>;
def SUB : ALU_rr<0b0100000, 0b000, "sub">;
def SLL : ALU_rr<0b0000000, 0b001, "sll">;
let IsSignExtendingOpW = 1 in {
  def SLT : ALU_rr<0b0000000, 0b010, "slt">;
  def SLTU : ALU_rr<0b0000000, 0b011, "sltu">;
}
def XOR : ALU_rr<0b0000000, 0b100, "xor", Commutable = 1>;
def SRL : ALU_rr<0b0000000, 0b101, "srl">;
def SRA : ALU_rr<0b0100000, 0b101, "sra">;
def OR : ALU_rr<0b0000000, 0b110, "or", Commutable = 1>;
def AND : ALU_rr<0b0000000, 0b111, "and", Commutable = 1>;

// This is a de facto standard (as set by GNU binutils) 32-bit unimplemented
// instruction (i.e., it should always trap, if your implementation has invalid
// instruction traps).
def UNIMP : VCSIRInstI<0b001, OPC_SYSTEM, (outs), (ins), "unimp", "">,
            Sched<[]> {
  let rs1 = 0;
  let rd = 0;
  let imm12 = 0b110000000000;
}

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions (User-Level ISA, Version 2.2, Chapter 20)
//===----------------------------------------------------------------------===//

// Note that the size is 32 because up to 8 32-bit instructions are needed to
// generate an arbitrary 64-bit immediate. However, the size does not really
// matter since PseudoLI is currently only used in the AsmParser where it gets
// expanded to real instructions immediately.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Size = 32, isCodeGenOnly = 0,
    isAsmParserOnly = 1 in def PseudoLI
    : Pseudo<(outs GPR:$rd), (ins ixlenimm_li:$imm), [], "li", "$rd, $imm">;

def PseudoLB : PseudoLoad<"lb">;
def PseudoLBU : PseudoLoad<"lbu">;
def PseudoLH : PseudoLoad<"lh">;
def PseudoLHU : PseudoLoad<"lhu">;
def PseudoLW : PseudoLoad<"lw">;

def PseudoSB : PseudoStore<"sb">;
def PseudoSH : PseudoStore<"sh">;
def PseudoSW : PseudoStore<"sw">;

def : InstAlias<"nop", (ADDI X0, X0, 0), 3>;
def : InstAlias<"li $rd, $imm", (ADDI GPR:$rd, X0, simm12:$imm), 2>;
def : InstAlias<"mv $rd, $rs", (ADDI GPR:$rd, GPR:$rs, 0)>;

def : InstAlias<"not $rd, $rs", (XORI GPR:$rd, GPR:$rs, -1)>;
def : InstAlias<"neg $rd, $rs", (SUB GPR:$rd, X0, GPR:$rs)>;

def : InstAlias<"seqz $rd, $rs", (SLTIU GPR:$rd, GPR:$rs, 1)>;
def : InstAlias<"snez $rd, $rs", (SLTU GPR:$rd, X0, GPR:$rs)>;
def : InstAlias<"sltz $rd, $rs", (SLT GPR:$rd, GPR:$rs, X0)>;
def : InstAlias<"sgtz $rd, $rs", (SLT GPR:$rd, X0, GPR:$rs)>;

// sgt/sgtu are recognised by the GNU assembler but the canonical slt/sltu
// form will always be printed. Therefore, set a zero weight.
def : InstAlias<"sgt $rd, $rs, $rt", (SLT GPR:$rd, GPR:$rt, GPR:$rs), 0>;
def : InstAlias<"sgtu $rd, $rs, $rt", (SLTU GPR:$rd, GPR:$rt, GPR:$rs), 0>;

def : InstAlias<"beqz $rs, $offset", (BEQ GPR:$rs, X0, simm13_lsb0:$offset)>;
def : InstAlias<"bnez $rs, $offset", (BNE GPR:$rs, X0, simm13_lsb0:$offset)>;
def : InstAlias<"blez $rs, $offset", (BGE X0, GPR:$rs, simm13_lsb0:$offset)>;
def : InstAlias<"bgez $rs, $offset", (BGE GPR:$rs, X0, simm13_lsb0:$offset)>;
def : InstAlias<"bltz $rs, $offset", (BLT GPR:$rs, X0, simm13_lsb0:$offset)>;
def : InstAlias<"bgtz $rs, $offset", (BLT X0, GPR:$rs, simm13_lsb0:$offset)>;

// Always output the canonical mnemonic for the pseudo branch instructions.
// The GNU tools emit the canonical mnemonic for the branch pseudo instructions
// as well (e.g. "bgt" will be recognised by the assembler but never printed by
// objdump). Match this behaviour by setting a zero weight.
def : InstAlias<"bgt $rs, $rt, $offset",
                (BLT GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;
def : InstAlias<"ble $rs, $rt, $offset",
                (BGE GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;
def : InstAlias<"bgtu $rs, $rt, $offset",
                (BLTU GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;
def : InstAlias<"bleu $rs, $rt, $offset",
                (BGEU GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;

def : InstAlias<"j $offset", (JAL X0, simm21_lsb0_jal:$offset)>;
def : InstAlias<"jal $offset", (JAL X1, simm21_lsb0_jal:$offset)>;

// Non-zero offset aliases of "jalr" are the lowest weight, followed by the
// two-register form, then the one-register forms and finally "ret".
def : InstAlias<"jr $rs", (JALR X0, GPR:$rs, 0), 3>;
def : InstAlias<"jr ${offset}(${rs})", (JALR X0, GPR:$rs, simm12:$offset)>;
def : InstAlias<"jalr $rs", (JALR X1, GPR:$rs, 0), 3>;
def : InstAlias<"jalr ${offset}(${rs})", (JALR X1, GPR:$rs, simm12:$offset)>;
def : InstAlias<"jalr $rd, $rs", (JALR GPR:$rd, GPR:$rs, 0), 2>;
def : InstAlias<"ret", (JALR X0, X1, 0), 4>;

// Non-canonical forms for jump targets also accepted by the assembler.
def : InstAlias<"jr $rs, $offset", (JALR X0, GPR:$rs, simm12:$offset), 0>;
def : InstAlias<"jalr $rs, $offset", (JALR X1, GPR:$rs, simm12:$offset), 0>;
def : InstAlias<"jalr $rd, $rs, $offset",
                (JALR GPR:$rd, GPR:$rs, simm12:$offset), 0>;
def : InstAlias<"jr (${rs})", (JALR X0, GPR:$rs, 0), 0>;
def : InstAlias<"jalr (${rs})", (JALR X1, GPR:$rs, 0), 0>;
def : InstAlias<"jalr $rd, (${rs})", (JALR GPR:$rd, GPR:$rs, 0), 0>;

let EmitPriority = 0 in {
  def : InstAlias<"lb $rd, (${rs1})", (LB GPR:$rd, GPR:$rs1, 0)>;
  def : InstAlias<"lh $rd, (${rs1})", (LH GPR:$rd, GPR:$rs1, 0)>;
  def : InstAlias<"lw $rd, (${rs1})", (LW GPR:$rd, GPR:$rs1, 0)>;
  def : InstAlias<"lbu $rd, (${rs1})", (LBU GPR:$rd, GPR:$rs1, 0)>;
  def : InstAlias<"lhu $rd, (${rs1})", (LHU GPR:$rd, GPR:$rs1, 0)>;

  def : InstAlias<"sb $rs2, (${rs1})", (SB GPR:$rs2, GPR:$rs1, 0)>;
  def : InstAlias<"sh $rs2, (${rs1})", (SH GPR:$rs2, GPR:$rs1, 0)>;
  def : InstAlias<"sw $rs2, (${rs1})", (SW GPR:$rs2, GPR:$rs1, 0)>;

  def : InstAlias<"add $rd, $rs1, $imm12", (ADDI GPR:$rd, GPR:$rs1,
                                               simm12:$imm12)>;
  def : InstAlias<"and $rd, $rs1, $imm12", (ANDI GPR:$rd, GPR:$rs1,
                                               simm12:$imm12)>;
  def : InstAlias<"xor $rd, $rs1, $imm12", (XORI GPR:$rd, GPR:$rs1,
                                               simm12:$imm12)>;
  def : InstAlias<"or $rd, $rs1, $imm12", (ORI GPR:$rd, GPR:$rs1,
                                              simm12:$imm12)>;
  def : InstAlias<"sll $rd, $rs1, $shamt", (SLLI GPR:$rd, GPR:$rs1,
                                               uimmlog2xlen:$shamt)>;
  def : InstAlias<"srl $rd, $rs1, $shamt", (SRLI GPR:$rd, GPR:$rs1,
                                               uimmlog2xlen:$shamt)>;
  def : InstAlias<"sra $rd, $rs1, $shamt", (SRAI GPR:$rd, GPR:$rs1,
                                               uimmlog2xlen:$shamt)>;
  def : InstAlias<"slt $rd, $rs1, $imm12", (SLTI GPR:$rd, GPR:$rs1,
                                               simm12:$imm12)>;
  def : InstAlias<"sltu $rd, $rs1, $imm12", (SLTIU GPR:$rd, GPR:$rs1,
                                                simm12:$imm12)>;
}

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//
// Naming convention: For 'generic' pattern classes, we use the naming
// convention PatTy1Ty2. For pattern classes which offer a more complex
// expansion, prefix the class name, e.g. BccPat.
//===----------------------------------------------------------------------===//

/// Generic pattern classes

class PatGpr<SDPatternOperator OpNode, VCSIRInst Inst, ValueType vt = i32>
    : Pat<(vt(OpNode(vt GPR:$rs1))), (Inst GPR:$rs1)>;
class PatGprGpr<SDPatternOperator OpNode, VCSIRInst Inst, ValueType vt1 = i32,
                ValueType vt2 = i32>
    : Pat<(vt1(OpNode(vt1 GPR:$rs1), (vt2 GPR:$rs2))), (Inst GPR:$rs1,
                                                           GPR:$rs2)>;

class PatGprImm<SDPatternOperator OpNode, VCSIRInst Inst, ImmLeaf ImmType,
                ValueType vt = i32>
    : Pat<(vt(OpNode(vt GPR:$rs1), ImmType:$imm)), (Inst GPR:$rs1,
                                                       ImmType:$imm)>;
class PatGprSimm12<SDPatternOperator OpNode, VCSIRInstI Inst>
    : PatGprImm<OpNode, Inst, simm12>;
class PatGprUimmLog2XLen<SDPatternOperator OpNode, VCSIRInstIShift Inst>
    : PatGprImm<OpNode, Inst, uimmlog2xlen>;

/// Predicates

def assertsexti32 : PatFrag<(ops node:$src), (assertsext node:$src), [{
  return cast<VTSDNode>(N->getOperand(1))->getVT().bitsLE(MVT::i32);
}]>;
def sexti16 : ComplexPattern<i32, 1, "selectSExtBits<16>">;

def sexti32 : ComplexPattern<i64, 1, "selectSExtBits<32>">;
def gi_sexti32 : GIComplexOperandMatcher<s64, "selectSExtBits<32>">,
                 GIComplexPatternEquiv<sexti32>;

def assertzexti32 : PatFrag<(ops node:$src), (assertzext node:$src), [{
  return cast<VTSDNode>(N->getOperand(1))->getVT().bitsLE(MVT::i32);
}]>;

def zexti32 : ComplexPattern<i64, 1, "selectZExtBits<32>">;
def gi_zexti32 : GIComplexOperandMatcher<s64, "selectZExtBits<32>">,
                 GIComplexPatternEquiv<zexti32>;

def zexti16 : ComplexPattern<i32, 1, "selectZExtBits<16>">;
def gi_zexti16 : GIComplexOperandMatcher<s32, "selectZExtBits<16>">,
                 GIComplexPatternEquiv<zexti16>;

def zexti8 : ComplexPattern<i32, 1, "selectZExtBits<8>">;
def gi_zexti8 : GIComplexOperandMatcher<s32, "selectZExtBits<8>">,
                GIComplexPatternEquiv<zexti8>;

def ext : PatFrags<(ops node:$A), [(sext node:$A), (zext node:$A)]>;

class binop_oneuse<SDPatternOperator operator>
    : PatFrag<(ops node:$A, node:$B), (operator node:$A, node:$B), [{
  return N->hasOneUse();
}]>;

def and_oneuse : binop_oneuse<and>;
def mul_oneuse : binop_oneuse<mul>;

def mul_const_oneuse
    : PatFrag<(ops node:$A, node:$B), (mul node:$A, node:$B), [{
  if (auto *N1C = dyn_cast<ConstantSDNode>(N->getOperand(1)))
    return N1C->hasOneUse();
  return false;
}]>;

class unop_oneuse<SDPatternOperator operator>
    : PatFrag<(ops node:$A), (operator node:$A), [{
  return N->hasOneUse();
}]>;

def sext_oneuse : unop_oneuse<sext>;
def zext_oneuse : unop_oneuse<zext>;
def anyext_oneuse : unop_oneuse<anyext>;
// def ext_oneuse    : unop_oneuse<ext>;

def 33signbits_node : PatLeaf<(i64 GPR:$src), [{
  return CurDAG->ComputeNumSignBits(SDValue(N, 0)) > 32;
}]>;

class immop_oneuse<ImmLeaf leaf> : PatLeaf<(leaf), [{
  return N->hasOneUse();
}]> {
  let GISelPredicateCode = [{
    return MRI.hasOneNonDBGUse(MI.getOperand(0).getReg());
  }];
}

/// Simple arithmetic operations

def : PatGprGpr<add, ADD>;
def : PatGprSimm12<add, ADDI>;
def : PatGprGpr<sub, SUB>;
def : PatGprGpr<or, OR>;
def : PatGprSimm12<or, ORI>;
def : PatGprGpr<and, AND>;
def : PatGprSimm12<and, ANDI>;
def : PatGprGpr<xor, XOR>;
def : PatGprSimm12<xor, XORI>;
def : PatGprUimmLog2XLen<shl, SLLI>;
def : PatGprUimmLog2XLen<srl, SRLI>;
def : PatGprUimmLog2XLen<sra, SRAI>;

// Match both a plain shift and one where the shift amount is masked (this is
// typically introduced when the legalizer promotes the shift amount and
// zero-extends it). For RISC-V, the mask is unnecessary as shifts in the base
// ISA only read the least significant 5 bits (RV32I) or 6 bits (RV64I).
def shiftMaskXLen : ComplexPattern<i32, 1, "selectShiftMaskXLen", [], [], 0>;
def shiftMask32 : ComplexPattern<i64, 1, "selectShiftMask32", [], [], 0>;
// FIXME: This is labelled as handling 's32', however the ComplexPattern it
// refers to handles both i32 and i64 based on the HwMode. Currently this LLT
// parameter appears to be ignored so this pattern works for both, however we
// should add a LowLevelTypeByHwMode, and use that to define our XLenLLT instead
// here.
def GIShiftMaskXLen : GIComplexOperandMatcher<s32, "selectShiftMaskXLen">,
                      GIComplexPatternEquiv<shiftMaskXLen>;
def GIShiftMask32 : GIComplexOperandMatcher<s64, "selectShiftMask32">,
                    GIComplexPatternEquiv<shiftMask32>;

class shiftop<SDPatternOperator operator>
    : PatFrag<(ops node:$val, node:$count),
              (operator node:$val, (i32(shiftMaskXLen node:$count)))>;
class shiftopw<SDPatternOperator operator>
    : PatFrag<(ops node:$val, node:$count),
              (operator node:$val, (i64(shiftMask32 node:$count)))>;

def : PatGprGpr<shiftop<shl>, SLL>;
def : PatGprGpr<shiftop<srl>, SRL>;
def : PatGprGpr<shiftop<sra>, SRA>;

/// FrameIndex calculations

// Transforms frameindex -> tframeindex.
def to_tframeindex : SDNodeXForm<frameindex, [{
  return CurDAG->getTargetFrameIndex(N->getIndex(), N->getValueType(0));
}]>;

def : GICustomOperandRenderer<"renderFrameIndex">,
      GISDNodeXFormEquiv<to_tframeindex>;

def : Pat<(frameindex:$fi), (ADDI(iPTR(to_tframeindex $fi)), 0)>;

def GIAddrRegImm : GIComplexOperandMatcher<s32, "selectAddrRegImm">,
                   GIComplexPatternEquiv<AddrRegImm>;

/// Stack probing

/// HI and ADD_LO address nodes.

// Pseudo for a rematerializable LUI+ADDI sequence for loading an address.
// It will be expanded after register allocation.
// FIXME: The scheduling information does not reflect the multiple instructions.
let Size = 8,
    isReMaterializable = 1 in def PseudoMovAddr
    : Pseudo<(outs GPR:$dst), (ins uimm20_lui:$hi, simm12:$lo), []>,
    Sched<[]>;

def vcsir_hi_oneuse : unop_oneuse<vcsir_hi>;
def addr_hi_lo : PatFrag<(ops node:$hi, node:$lo),
                         (vcsir_add_lo(vcsir_hi_oneuse node:$hi), node:$lo)>;

def : Pat<(addr_hi_lo tglobaladdr:$hi, tglobaladdr:$lo),
          (PseudoMovAddr tglobaladdr:$hi, tglobaladdr:$lo)>;
def : Pat<(addr_hi_lo tblockaddress:$hi, tblockaddress:$lo),
          (PseudoMovAddr tblockaddress:$hi, tblockaddress:$lo)>;
def : Pat<(addr_hi_lo tjumptable:$hi, tjumptable:$lo),
          (PseudoMovAddr tjumptable:$hi, tjumptable:$lo)>;
def : Pat<(addr_hi_lo tconstpool:$hi, tconstpool:$lo),
          (PseudoMovAddr tconstpool:$hi, tconstpool:$lo)>;

def : Pat<(vcsir_hi tglobaladdr:$in), (LUI tglobaladdr:$in)>;
def : Pat<(vcsir_hi tblockaddress:$in), (LUI tblockaddress:$in)>;
def : Pat<(vcsir_hi tjumptable:$in), (LUI tjumptable:$in)>;
def : Pat<(vcsir_hi tconstpool:$in), (LUI tconstpool:$in)>;

def : Pat<(vcsir_add_lo GPR:$hi, tglobaladdr:$lo), (ADDI GPR:$hi,
                                                       tglobaladdr:$lo)>;
def : Pat<(vcsir_add_lo GPR:$hi, tblockaddress:$lo), (ADDI GPR:$hi,
                                                         tblockaddress:$lo)>;
def : Pat<(vcsir_add_lo GPR:$hi, tjumptable:$lo), (ADDI GPR:$hi,
                                                      tjumptable:$lo)>;
def : Pat<(vcsir_add_lo GPR:$hi, tconstpool:$lo), (ADDI GPR:$hi,
                                                      tconstpool:$lo)>;

/// Setcc

def : PatGprGpr<setlt, SLT>;
def : PatGprSimm12<setlt, SLTI>;
def : PatGprGpr<setult, SLTU>;
def : PatGprSimm12<setult, SLTIU>;

// RISC-V doesn't have general instructions for integer setne/seteq, but we can
// check for equality with 0. These ComplexPatterns rewrite the setne/seteq into
// something that can be compared with 0.
// These ComplexPatterns must be used in pairs.
def vcsir_setne : ComplexPattern<i32, 1, "selectSETNE", [setcc]>;
def vcsir_seteq : ComplexPattern<i32, 1, "selectSETEQ", [setcc]>;

// Define pattern expansions for setcc operations that aren't directly
// handled by a RISC-V instruction.
def : Pat<(vcsir_seteq(i32 GPR:$rs1)), (SLTIU GPR:$rs1, 1)>;
def : Pat<(vcsir_setne(i32 GPR:$rs1)), (SLTU(i32 X0), GPR:$rs1)>;
def : Pat<(i32(setne(i32 GPR:$rs1), -1)), (SLTIU GPR:$rs1, -1)>;

def IntCCtoVCSIRCC : SDNodeXForm<vcsir_selectcc, [{
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  VCSIRCC::CondCode BrCC = getVCSIRCCForIntCC(CC);
  return CurDAG->getTargetConstant(BrCC, SDLoc(N), Subtarget->geti32());
}]>;

/// Branches and jumps

let isBarrier = 1, isBranch = 1,
    isTerminator = 1 in def PseudoBR
    : Pseudo<(outs), (ins simm21_lsb0_jal:$imm20), [(br bb:$imm20)]>,
    PseudoInstExpansion<(JAL X0, simm21_lsb0_jal:$imm20)>;

// PseudoCALL is a pseudo instruction which will eventually expand to auipc
// and jalr while encoding. This is desirable, as an auipc+jalr pair with
// R_VCSIR_CALL and R_VCSIR_RELAX relocations can be be relaxed by the linker
// if the offset fits in a signed 21-bit immediate.
// Define AsmString to print "call" when compile with -S flag.
// Define isCodeGenOnly = 0 to support parsing assembly "call" instruction.
let isCall = 1, Defs = [X1], isCodeGenOnly = 0,
    Size = 8 in def PseudoCALL
    : Pseudo<(outs), (ins call_symbol:$func), [], "call", "$func">,
    Sched<[]>;

let isBarrier = 1, isReturn = 1,
    isTerminator =
        1 in def PseudoRET : Pseudo<(outs), (ins), [(vcsir_ret_glue)]>,
    PseudoInstExpansion<(JALR X0, X1, 0)>;

let isCall = 0, isBarrier = 1, isBranch = 1, isTerminator = 1, Size = 8,
    isCodeGenOnly = 0, hasSideEffects = 0, mayStore = 0,
    mayLoad = 0 in def PseudoJump
    : Pseudo<(outs GPR:$rd), (ins pseudo_jump_symbol:$target), [], "jump",
             "$target, $rd">,
    Sched<[]>;

// Pseudo for a rematerializable constant materialization sequence.
// This is an experimental feature enabled by
// -vcsir-use-rematerializable-movimm in VCSIRISelDAGToDAG.cpp
// It will be expanded after register allocation.
// FIXME: The scheduling information does not reflect the multiple instructions.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Size = 8,
    isReMaterializable =
        1 in def PseudoMovImm : Pseudo<(outs GPR:$dst), (ins i32imm:$imm), []>,
    Sched<[]>;
/// Loads

class LdPat<PatFrag LoadOp, VCSIRInst Inst, ValueType vt = i32>
    : Pat<(vt(LoadOp(AddrRegImm(i32 GPR:$rs1), simm12:$imm12))),
          (Inst GPR:$rs1, simm12:$imm12)>;

def : LdPat<sextloadi8, LB>;
def : LdPat<extloadi8, LBU>; // Prefer unsigned due to no c.lb in Zcb.
def : LdPat<sextloadi16, LH>;
def : LdPat<extloadi16, LH>;
def : LdPat<load, LW, i32>;
def : LdPat<zextloadi8, LBU>;
def : LdPat<zextloadi16, LHU>;

/// Stores

class StPat<PatFrag StoreOp, VCSIRInst Inst, RegisterClass StTy, ValueType vt>
    : Pat<(StoreOp(vt StTy:$rs2), (AddrRegImm(i32 GPR:$rs1), simm12:$imm12)),
          (Inst StTy:$rs2, GPR:$rs1, simm12:$imm12)>;

/// Other pseudo-instructions

// Pessimistically assume the stack pointer will be clobbered
let Defs = [X2], Uses = [X2] in {
  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                [(callseq_start timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              [(callseq_end timm:$amt1, timm:$amt2)]>;
} // Defs = [X2], Uses = [X2]
