//===-- VCSIRISelLowering.cpp - VCSIR DAG Lowering Implementation  -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that VCSIR uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "VCSIRISelLowering.h"
#include "MCTargetDesc/VCSIRMatInt.h"
/// #include "VCSIR.h"
// #include "VCSIRConstantPoolValue.h"
#include "VCSIRMachineFunctionInfo.h"
#include "VCSIRRegisterInfo.h"
// #include "VCSIRSelectionDAGInfo.h"
#include "VCSIRSubtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGAddressAnalysis.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Target/TargetMachine.h"
// #include "llvm/IR/IntrinsicsVCSIR.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/InstructionCost.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "vcsir-lower"

VCSIRTargetLowering::VCSIRTargetLowering(const TargetMachine &TM,
                                         const VCSIRSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {

  MVT XLenVT = MVT::i32;

  // Set up the register classes.
  addRegisterClass(XLenVT, &VCSIR::GPRRegClass);

  // Compute derived properties from the register classes.
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(VCSIR::X2);

  setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, XLenVT,
                   MVT::i1, Promote);
  // DAGCombiner can call isLoadExtLegal for types that aren't legal.
  setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, MVT::i32,
                   MVT::i1, Promote);

  // TODO: add all necessary setOperationAction calls.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, XLenVT, Custom);

  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BR_CC, XLenVT, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  setOperationAction(ISD::SELECT_CC, XLenVT, Expand);

  setCondCodeAction(ISD::SETGT, XLenVT, Custom);
  setCondCodeAction(ISD::SETGE, XLenVT, Expand);
  setCondCodeAction(ISD::SETUGT, XLenVT, Custom);
  setCondCodeAction(ISD::SETUGE, XLenVT, Expand);

  setOperationAction({ISD::STACKSAVE, ISD::STACKRESTORE}, MVT::Other, Expand);

  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction({ISD::VAARG, ISD::VACOPY, ISD::VAEND}, MVT::Other, Expand);

  setOperationAction(ISD::EH_DWARF_CFA, MVT::i32, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, {MVT::i8, MVT::i16}, Expand);

  setOperationAction(ISD::MUL, MVT::i64, Custom);

  if (true /*!Has Ext M*/) {
    setOperationAction({ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM}, XLenVT,
                       Expand);
  }

  setOperationAction(
      {ISD::SDIVREM, ISD::UDIVREM, ISD::SMUL_LOHI, ISD::UMUL_LOHI}, XLenVT,
      Expand);

  setOperationAction({ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS}, XLenVT,
                     Custom);

  setOperationAction({ISD::ROTL, ISD::ROTR}, XLenVT, Expand);

  setOperationAction(ISD::BSWAP, XLenVT, Expand);

  setOperationAction(ISD::BITREVERSE, XLenVT, Expand);

  setOperationAction({ISD::CTTZ, ISD::CTPOP}, XLenVT, Expand);
  setOperationAction(ISD::CTLZ, XLenVT, Expand);

  setOperationAction(ISD::SELECT, XLenVT, Custom);

  setOperationAction({ISD::GlobalAddress, ISD::BlockAddress, ISD::ConstantPool,
                      ISD::JumpTable},
                     XLenVT, Custom);

  setOperationAction(ISD::GlobalTLSAddress, XLenVT, Custom);

  setMaxAtomicSizeInBitsSupported(0);

  setBooleanContents(ZeroOrOneBooleanContent);

  if (getTargetMachine().getTargetTriple().isOSLinux()) {
    // Custom lowering of llvm.clear_cache.
    setOperationAction(ISD::CLEAR_CACHE, MVT::Other, Custom);
  }

  // Function alignments.
  const Align FunctionAlignment(4);
  setMinFunctionAlignment(FunctionAlignment);
  // Set preferred alignments.
  setPrefFunctionAlignment(Align(4));
  setPrefLoopAlignment(Align(4));

  setTargetDAGCombine({ISD::INTRINSIC_VOID, ISD::INTRINSIC_W_CHAIN,
                       ISD::INTRINSIC_WO_CHAIN, ISD::ADD, ISD::SUB, ISD::MUL,
                       ISD::AND, ISD::OR, ISD::XOR, ISD::SETCC, ISD::SELECT});
  setTargetDAGCombine(ISD::SRA);
  setTargetDAGCombine(ISD::SIGN_EXTEND_INREG);

  // Disable strict node mutation.
  IsStrictFPEnabled = true;
  EnableExtLdPromotion = true;

  // Let the subtarget decide if a predictable select is more expensive than the
  // corresponding branch. This information is used in CGP/SelectOpt to decide
  // when to convert selects into branches.
  PredictableSelectIsExpensive = false;

  MaxStoresPerMemsetOptSize = 4;
  MaxStoresPerMemset = 4;

  MaxGluedStoresPerMemcpy = 4;
  MaxStoresPerMemcpyOptSize = 4;
  MaxStoresPerMemcpy = 4;

  MaxStoresPerMemmoveOptSize = 4;
  ;
  MaxStoresPerMemmove = 4;

  MaxLoadsPerMemcmpOptSize = 4;
  MaxLoadsPerMemcmp = 4;
}

static SDValue lowerConstant(SDValue Op, SelectionDAG &DAG,
                             const VCSIRSubtarget &Subtarget) {
  int64_t Imm = cast<ConstantSDNode>(Op)->getSExtValue();

  // All simm32 constants should be handled by isel.
  // NOTE: The getMaxBuildIntsCost call below should return a value >= 2 making
  // this check redundant, but small immediates are common so this check
  // should have better compile time.
  if (isInt<32>(Imm))
    return Op;
  return SDValue();
}
SDValue VCSIRTargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  BlockAddressSDNode *N = cast<BlockAddressSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue VCSIRTargetLowering::lowerConstantPool(SDValue Op,
                                               SelectionDAG &DAG) const {
  ConstantPoolSDNode *N = cast<ConstantPoolSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue VCSIRTargetLowering::lowerJumpTable(SDValue Op,
                                            SelectionDAG &DAG) const {
  JumpTableSDNode *N = cast<JumpTableSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue VCSIRTargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  assert(N->getOffset() == 0 && "unexpected offset in global node");
  const GlobalValue *GV = N->getGlobal();
  return getAddr(N, DAG, GV->isDSOLocal(), GV->hasExternalWeakLinkage());
}
SDValue VCSIRTargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    report_fatal_error("unimplemented operand");
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:
    return lowerConstantPool(Op, DAG);
  case ISD::JumpTable:
    return lowerJumpTable(Op, DAG);
  case ISD::Constant:
    return lowerConstant(Op, DAG, Subtarget);
  case ISD::SELECT:
    return lowerSELECT(Op, DAG);
  case ISD::BRCOND:
    return lowerBRCOND(Op, DAG);
  case ISD::FRAMEADDR:
    return lowerFRAMEADDR(Op, DAG);
  case ISD::RETURNADDR:
    return lowerRETURNADDR(Op, DAG);
  case ISD::BITCAST: {
    SDLoc DL(Op);
    EVT VT = Op.getValueType();
    SDValue Op0 = Op.getOperand(0);
    EVT Op0VT = Op0.getValueType();
    if (!VT.isVector() && !Op0VT.isVector()) {
      if (isTypeLegal(VT) && isTypeLegal(Op0VT))
        return Op;
      return SDValue();
    }
    return SDValue();
  }
  case ISD::TRUNCATE:
  case ISD::TRUNCATE_SSAT_S:
  case ISD::TRUNCATE_USAT_U:
    return Op;
  case ISD::ANY_EXTEND:
  case ISD::LOAD:
  case ISD::STORE:
    return Op;
  case ISD::SELECT_CC: {
    // This occurs because we custom legalize SETGT and SETUGT for setcc. That
    // causes LegalizeDAG to think we need to custom legalize select_cc. Expand
    // into separate SETCC+SELECT just like LegalizeDAG.
    SDValue Tmp1 = Op.getOperand(0);
    SDValue Tmp2 = Op.getOperand(1);
    SDValue True = Op.getOperand(2);
    SDValue False = Op.getOperand(3);
    EVT VT = Op.getValueType();
    SDValue CC = Op.getOperand(4);
    EVT CmpVT = Tmp1.getValueType();
    EVT CCVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), CmpVT);
    SDLoc DL(Op);
    SDValue Cond =
        DAG.getNode(ISD::SETCC, DL, CCVT, Tmp1, Tmp2, CC, Op->getFlags());
    return DAG.getSelect(DL, VT, Cond, True, False);
  }
  case ISD::SETCC: {
    MVT OpVT = Op.getOperand(0).getSimpleValueType();
    MVT VT = Op.getSimpleValueType();
    SDValue LHS = Op.getOperand(0);
    SDValue RHS = Op.getOperand(1);
    ISD::CondCode CCVal = cast<CondCodeSDNode>(Op.getOperand(2))->get();
    assert((CCVal == ISD::SETGT || CCVal == ISD::SETUGT) &&
           "Unexpected CondCode");

    SDLoc DL(Op);

    // If the RHS is a constant in the range [-2049, 0) or (0, 2046], we can
    // convert this to the equivalent of (set(u)ge X, C+1) by using
    // (xori (slti(u) X, C+1), 1). This avoids materializing a small constant
    // in a register.
    if (isa<ConstantSDNode>(RHS)) {
      int64_t Imm = cast<ConstantSDNode>(RHS)->getSExtValue();
      if (Imm != 0 && isInt<12>((uint64_t)Imm + 1)) {
        // If this is an unsigned compare and the constant is -1, incrementing
        // the constant would change behavior. The result should be false.
        if (CCVal == ISD::SETUGT && Imm == -1)
          return DAG.getConstant(0, DL, VT);
        // Using getSetCCSwappedOperands will convert SET(U)GT->SET(U)LT.
        CCVal = ISD::getSetCCSwappedOperands(CCVal);
        SDValue SetCC = DAG.getSetCC(
            DL, VT, LHS, DAG.getSignedConstant(Imm + 1, DL, OpVT), CCVal);
        return DAG.getLogicalNOT(DL, SetCC, VT);
      }
    }

    // Not a constant we could handle, swap the operands and condition code to
    // SETLT/SETULT.
    CCVal = ISD::getSetCCSwappedOperands(CCVal);
    return DAG.getSetCC(DL, VT, RHS, LHS, CCVal);
  }
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SDIV:
  case ISD::SREM:
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::CTPOP:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
  case ISD::UADDSAT:
  case ISD::USUBSAT:
  case ISD::SADDSAT:
  case ISD::SSUBSAT:
    return Op;
  case ISD::ABDS:
  case ISD::ABDU: {
    SDLoc dl(Op);
    EVT VT = Op->getValueType(0);
    SDValue LHS = DAG.getFreeze(Op->getOperand(0));
    SDValue RHS = DAG.getFreeze(Op->getOperand(1));
    bool IsSigned = Op->getOpcode() == ISD::ABDS;

    // abds(lhs, rhs) -> sub(smax(lhs,rhs), smin(lhs,rhs))
    // abdu(lhs, rhs) -> sub(umax(lhs,rhs), umin(lhs,rhs))
    unsigned MaxOpc = IsSigned ? ISD::SMAX : ISD::UMAX;
    unsigned MinOpc = IsSigned ? ISD::SMIN : ISD::UMIN;
    SDValue Max = DAG.getNode(MaxOpc, dl, VT, LHS, RHS);
    SDValue Min = DAG.getNode(MinOpc, dl, VT, LHS, RHS);
    return DAG.getNode(ISD::SUB, dl, VT, Max, Min);
  }
  }
}
// Return true if Val is equal to (setcc LHS, RHS, CC).
// Return false if Val is the inverse of (setcc LHS, RHS, CC).
// Otherwise, return std::nullopt.
static std::optional<bool> matchSetCC(SDValue LHS, SDValue RHS,
                                      ISD::CondCode CC, SDValue Val) {
  assert(Val->getOpcode() == ISD::SETCC);
  SDValue LHS2 = Val.getOperand(0);
  SDValue RHS2 = Val.getOperand(1);
  ISD::CondCode CC2 = cast<CondCodeSDNode>(Val.getOperand(2))->get();

  if (LHS == LHS2 && RHS == RHS2) {
    if (CC == CC2)
      return true;
    if (CC == ISD::getSetCCInverse(CC2, LHS2.getValueType()))
      return false;
  } else if (LHS == RHS2 && RHS == LHS2) {
    CC2 = ISD::getSetCCSwappedOperands(CC2);
    if (CC == CC2)
      return true;
    if (CC == ISD::getSetCCInverse(CC2, LHS2.getValueType()))
      return false;
  }

  return std::nullopt;
}
static SDValue combineSelectToBinOp(SDNode *N, SelectionDAG &DAG,
                                    const VCSIRSubtarget &Subtarget) {
  SDValue CondV = N->getOperand(0);
  SDValue TrueV = N->getOperand(1);
  SDValue FalseV = N->getOperand(2);
  MVT VT = N->getSimpleValueType(0);
  SDLoc DL(N);

  // select c, ~x, x --> xor -c, x
  if (isa<ConstantSDNode>(TrueV) && isa<ConstantSDNode>(FalseV)) {
    const APInt &TrueVal = TrueV->getAsAPIntVal();
    const APInt &FalseVal = FalseV->getAsAPIntVal();
    if (~TrueVal == FalseVal) {
      SDValue Neg = DAG.getNegative(CondV, DL, VT);
      return DAG.getNode(ISD::XOR, DL, VT, Neg, FalseV);
    }
  }

  // Try to fold (select (setcc lhs, rhs, cc), truev, falsev) into bitwise ops
  // when both truev and falsev are also setcc.
  if (CondV.getOpcode() == ISD::SETCC && TrueV.getOpcode() == ISD::SETCC &&
      FalseV.getOpcode() == ISD::SETCC) {
    SDValue LHS = CondV.getOperand(0);
    SDValue RHS = CondV.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(CondV.getOperand(2))->get();

    // (select x, x, y) -> x | y
    // (select !x, x, y) -> x & y
    if (std::optional<bool> MatchResult = matchSetCC(LHS, RHS, CC, TrueV)) {
      return DAG.getNode(*MatchResult ? ISD::OR : ISD::AND, DL, VT, TrueV,
                         DAG.getFreeze(FalseV));
    }
    // (select x, y, x) -> x & y
    // (select !x, y, x) -> x | y
    if (std::optional<bool> MatchResult = matchSetCC(LHS, RHS, CC, FalseV)) {
      return DAG.getNode(*MatchResult ? ISD::AND : ISD::OR, DL, VT,
                         DAG.getFreeze(TrueV), FalseV);
    }
  }

  return SDValue();
}

// Changes the condition code and swaps operands if necessary, so the SetCC
// operation matches one of the comparisons supported directly by branches
// in the VCSIR ISA. May adjust compares to favor compare with 0 over compare
// with 1/-1.
static void translateSetCCForBranch(const SDLoc &DL, SDValue &LHS, SDValue &RHS,
                                    ISD::CondCode &CC, SelectionDAG &DAG) {
  // If this is a single bit test that can't be handled by ANDI, shift the
  // bit to be tested to the MSB and perform a signed compare with 0.
  if (isIntEqualitySetCC(CC) && isNullConstant(RHS) &&
      LHS.getOpcode() == ISD::AND && LHS.hasOneUse() &&
      isa<ConstantSDNode>(LHS.getOperand(1))) {
    uint64_t Mask = LHS.getConstantOperandVal(1);
    if ((isPowerOf2_64(Mask) || isMask_64(Mask)) && !isInt<12>(Mask)) {
      unsigned ShAmt = 0;
      if (isPowerOf2_64(Mask)) {
        CC = CC == ISD::SETEQ ? ISD::SETGE : ISD::SETLT;
        ShAmt = LHS.getValueSizeInBits() - 1 - Log2_64(Mask);
      } else {
        ShAmt = LHS.getValueSizeInBits() - llvm::bit_width(Mask);
      }

      LHS = LHS.getOperand(0);
      if (ShAmt != 0)
        LHS = DAG.getNode(ISD::SHL, DL, LHS.getValueType(), LHS,
                          DAG.getConstant(ShAmt, DL, LHS.getValueType()));
      return;
    }
  }

  if (auto *RHSC = dyn_cast<ConstantSDNode>(RHS)) {
    int64_t C = RHSC->getSExtValue();
    switch (CC) {
    default:
      break;
    case ISD::SETGT:
      // Convert X > -1 to X >= 0.
      if (C == -1) {
        RHS = DAG.getConstant(0, DL, RHS.getValueType());
        CC = ISD::SETGE;
        return;
      }
      break;
    case ISD::SETLT:
      // Convert X < 1 to 0 >= X.
      if (C == 1) {
        RHS = LHS;
        LHS = DAG.getConstant(0, DL, RHS.getValueType());
        CC = ISD::SETGE;
        return;
      }
      break;
    }
  }

  switch (CC) {
  default:
    break;
  case ISD::SETGT:
  case ISD::SETLE:
  case ISD::SETUGT:
  case ISD::SETULE:
    CC = ISD::getSetCCSwappedOperands(CC);
    std::swap(LHS, RHS);
    break;
  }
}

SDValue VCSIRTargetLowering::lowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDValue CondV = Op.getOperand(0);
  SDValue TrueV = Op.getOperand(1);
  SDValue FalseV = Op.getOperand(2);
  SDLoc DL(Op);
  MVT VT = Op.getSimpleValueType();
  MVT XLenVT = Subtarget.getXLenVT();

  if (SDValue V = combineSelectToBinOp(Op.getNode(), DAG, Subtarget))
    return V;

  // If the condition is not an integer SETCC which operates on XLenVT, we need
  // to emit a VCSIRISD::SELECT_CC comparing the condition to zero. i.e.:
  // (select condv, truev, falsev)
  // -> (vcsirisd::select_cc condv, zero, setne, truev, falsev)
  if (CondV.getOpcode() != ISD::SETCC ||
      CondV.getOperand(0).getSimpleValueType() != XLenVT) {
    SDValue Zero = DAG.getConstant(0, DL, XLenVT);
    SDValue SetNE = DAG.getCondCode(ISD::SETNE);

    SDValue Ops[] = {CondV, Zero, SetNE, TrueV, FalseV};

    return DAG.getNode(VCSIRISD::SELECT_CC, DL, VT, Ops);
  }

  // If the CondV is the output of a SETCC node which operates on XLenVT inputs,
  // then merge the SETCC node into the lowered VCSIRISD::SELECT_CC to take
  // advantage of the integer compare+branch instructions. i.e.:
  // (select (setcc lhs, rhs, cc), truev, falsev)
  // -> (vcsirisd::select_cc lhs, rhs, cc, truev, falsev)
  SDValue LHS = CondV.getOperand(0);
  SDValue RHS = CondV.getOperand(1);
  ISD::CondCode CCVal = cast<CondCodeSDNode>(CondV.getOperand(2))->get();

  // Special case for a select of 2 constants that have a difference of 1.
  // Normally this is done by DAGCombine, but if the select is introduced by
  // type legalization or op legalization, we miss it. Restricting to SETLT
  // case for now because that is what signed saturating add/sub need.
  // FIXME: We don't need the condition to be SETLT or even a SETCC,
  // but we would probably want to swap the true/false values if the condition
  // is SETGE/SETLE to avoid an XORI.
  if (isa<ConstantSDNode>(TrueV) && isa<ConstantSDNode>(FalseV) &&
      CCVal == ISD::SETLT) {
    const APInt &TrueVal = TrueV->getAsAPIntVal();
    const APInt &FalseVal = FalseV->getAsAPIntVal();
    if (TrueVal - 1 == FalseVal)
      return DAG.getNode(ISD::ADD, DL, VT, CondV, FalseV);
    if (TrueVal + 1 == FalseVal)
      return DAG.getNode(ISD::SUB, DL, VT, FalseV, CondV);
  }

  translateSetCCForBranch(DL, LHS, RHS, CCVal, DAG);
  // 1 < x ? x : 1 -> 0 < x ? x : 1
  if (isOneConstant(LHS) && (CCVal == ISD::SETLT || CCVal == ISD::SETULT) &&
      RHS == TrueV && LHS == FalseV) {
    LHS = DAG.getConstant(0, DL, VT);
    // 0 <u x is the same as x != 0.
    if (CCVal == ISD::SETULT) {
      std::swap(LHS, RHS);
      CCVal = ISD::SETNE;
    }
  }

  // x <s -1 ? x : -1 -> x <s 0 ? x : -1
  if (isAllOnesConstant(RHS) && CCVal == ISD::SETLT && LHS == TrueV &&
      RHS == FalseV) {
    RHS = DAG.getConstant(0, DL, VT);
  }

  SDValue TargetCC = DAG.getCondCode(CCVal);

  if (isa<ConstantSDNode>(TrueV) && !isa<ConstantSDNode>(FalseV)) {
    // (select (setcc lhs, rhs, CC), constant, falsev)
    // -> (select (setcc lhs, rhs, InverseCC), falsev, constant)
    std::swap(TrueV, FalseV);
    TargetCC = DAG.getCondCode(ISD::getSetCCInverse(CCVal, LHS.getValueType()));
  }

  SDValue Ops[] = {LHS, RHS, TargetCC, TrueV, FalseV};
  return DAG.getNode(VCSIRISD::SELECT_CC, DL, VT, Ops);
}

SDValue VCSIRTargetLowering::lowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  SDValue CondV = Op.getOperand(1);
  SDLoc DL(Op);
  MVT XLenVT = Subtarget.getXLenVT();

  if (CondV.getOpcode() == ISD::SETCC &&
      CondV.getOperand(0).getValueType() == XLenVT) {
    SDValue LHS = CondV.getOperand(0);
    SDValue RHS = CondV.getOperand(1);
    ISD::CondCode CCVal = cast<CondCodeSDNode>(CondV.getOperand(2))->get();

    translateSetCCForBranch(DL, LHS, RHS, CCVal, DAG);

    SDValue TargetCC = DAG.getCondCode(CCVal);
    return DAG.getNode(VCSIRISD::BR_CC, DL, Op.getValueType(), Op.getOperand(0),
                       LHS, RHS, TargetCC, Op.getOperand(2));
  }

  return DAG.getNode(VCSIRISD::BR_CC, DL, Op.getValueType(), Op.getOperand(0),
                     CondV, DAG.getConstant(0, DL, XLenVT),
                     DAG.getCondCode(ISD::SETNE), Op.getOperand(2));
}
SDValue VCSIRTargetLowering::lowerFRAMEADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  const VCSIRRegisterInfo &RI = *Subtarget.getRegisterInfo();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);
  Register FrameReg = RI.getFrameRegister(MF);
  int XLenInBytes = Subtarget.getXLen() / 8;

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), DL, FrameReg, VT);
  unsigned Depth = Op.getConstantOperandVal(0);
  while (Depth--) {
    int Offset = -(XLenInBytes * 2);
    SDValue Ptr = DAG.getNode(
        ISD::ADD, DL, VT, FrameAddr,
        DAG.getSignedConstant(Offset, DL, getPointerTy(DAG.getDataLayout())));
    FrameAddr =
        DAG.getLoad(VT, DL, DAG.getEntryNode(), Ptr, MachinePointerInfo());
  }
  return FrameAddr;
}

SDValue VCSIRTargetLowering::lowerRETURNADDR(SDValue Op,
                                             SelectionDAG &DAG) const {
  const VCSIRRegisterInfo &RI = *Subtarget.getRegisterInfo();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);
  MVT XLenVT = Subtarget.getXLenVT();
  int XLenInBytes = Subtarget.getXLen() / 8;

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Depth = Op.getConstantOperandVal(0);
  if (Depth) {
    int Off = -XLenInBytes;
    SDValue FrameAddr = lowerFRAMEADDR(Op, DAG);
    SDValue Offset = DAG.getSignedConstant(Off, DL, VT);
    return DAG.getLoad(VT, DL, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, DL, VT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Return the value of the return address register, marking it an implicit
  // live-in.
  Register Reg = MF.addLiveIn(RI.getRARegister(), getRegClassFor(XLenVT));
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, XLenVT);
}

EVT VCSIRTargetLowering::getSetCCResultType(const DataLayout &DL,
                                            LLVMContext &Context,
                                            EVT VT) const {
  return getPointerTy(DL);
}

bool VCSIRTargetLowering::shouldExpandCttzElements(EVT VT) const {
  return true;
}

bool VCSIRTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                const AddrMode &AM, Type *Ty,
                                                unsigned AS,
                                                Instruction *I) const {
  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  // None of our addressing modes allows a scalable offset
  if (AM.ScalableOffset)
    return false;

  // Require a 12-bit signed offset.
  if (!isInt<12>(AM.BaseOffs))
    return false;

  switch (AM.Scale) {
  case 0: // "r+i" or just "i", depending on HasBaseReg.
    break;
  case 1:
    if (!AM.HasBaseReg) // allow "r+i".
      break;
    return false; // disallow "r+r" or "r+r+i".
  default:
    return false;
  }

  return true;
}

bool VCSIRTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  return isInt<12>(Imm);
}

bool VCSIRTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  return isInt<12>(Imm);
}

// On RV32, 64-bit integers are split into their high and low parts and held
// in two different registers, so the trunc is free since the low register can
// just be used.
// FIXME: Should we consider i64->i32 free on RV64 to match the EVT version of
// isTruncateFree?
bool VCSIRTargetLowering::isTruncateFree(Type *SrcTy, Type *DstTy) const {
  if (!SrcTy->isIntegerTy() || !DstTy->isIntegerTy())
    return false;
  unsigned SrcBits = SrcTy->getPrimitiveSizeInBits();
  unsigned DestBits = DstTy->getPrimitiveSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool VCSIRTargetLowering::isTruncateFree(EVT SrcVT, EVT DstVT) const {
  // We consider i64->i32 free on RV64 since we have good selection of W
  // instructions that make promoting operations back to i64 free in many cases.
  if (SrcVT.isVector() || DstVT.isVector() || !SrcVT.isInteger() ||
      !DstVT.isInteger())
    return false;
  unsigned SrcBits = SrcVT.getSizeInBits();
  unsigned DestBits = DstVT.getSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool VCSIRTargetLowering::isTruncateFree(SDValue Val, EVT VT2) const {
  // free truncate from vnsrl and vnsra
  return TargetLowering::isTruncateFree(Val, VT2);
}

bool VCSIRTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  // Zexts are free if they can be combined with a load.
  // Don't advertise i32->i64 zextload as being free for RV64. It interacts
  // poorly with type legalization of compares preferring sext.
  if (auto *LD = dyn_cast<LoadSDNode>(Val)) {
    EVT MemVT = LD->getMemoryVT();
    if ((MemVT == MVT::i8 || MemVT == MVT::i16) &&
        (LD->getExtensionType() == ISD::NON_EXTLOAD ||
         LD->getExtensionType() == ISD::ZEXTLOAD))
      return true;
  }

  return TargetLowering::isZExtFree(Val, VT2);
}

unsigned
VCSIRTargetLowering::getNumRegisters(LLVMContext &Context, EVT VT,
                                     std::optional<MVT> RegisterVT) const {
  // Pair inline assembly operand
  if (VT == MVT::i64 && RegisterVT && *RegisterVT == MVT::Untyped)
    return 1;

  return TargetLowering::getNumRegisters(Context, VT, RegisterVT);
}

void VCSIRTargetLowering::ReplaceNodeResults(SDNode *N,
                                             SmallVectorImpl<SDValue> &Results,
                                             SelectionDAG &DAG) const {
  SDLoc DL(N);
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Don't know how to custom type legalize this operation!");
  case ISD::LOAD: {
    if (!ISD::isNON_EXTLoad(N))
      return;

    // Use a SEXTLOAD instead of the default EXTLOAD. Similar to the
    // sext_inreg we emit for ADD/SUB/MUL/SLLI.
    LoadSDNode *Ld = cast<LoadSDNode>(N);

    SDLoc dl(N);
    SDValue Res = DAG.getExtLoad(ISD::SEXTLOAD, dl, MVT::i64, Ld->getChain(),
                                 Ld->getBasePtr(), Ld->getMemoryVT(),
                                 Ld->getMemOperand());
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Res));
    Results.push_back(Res.getValue(1));
    return;
  }
  case ISD::MUL: {
    unsigned Size = N->getSimpleValueType(0).getSizeInBits();
    unsigned XLen = 32;
    // This multiply needs to be expanded, try to use MULHSU+MUL if possible.
    if (Size > XLen) {
      assert(Size == (XLen * 2) && "Unexpected custom legalisation");
      SDValue LHS = N->getOperand(0);
      SDValue RHS = N->getOperand(1);
      APInt HighMask = APInt::getHighBitsSet(Size, XLen);

      bool LHSIsU = DAG.MaskedValueIsZero(LHS, HighMask);
      bool RHSIsU = DAG.MaskedValueIsZero(RHS, HighMask);
      // We need exactly one side to be unsigned.
      if (LHSIsU == RHSIsU)
        return;

      auto MakeMULPair = [&](SDValue S, SDValue U) {
        MVT XLenVT = MVT::i32;
        ;
        S = DAG.getNode(ISD::TRUNCATE, DL, XLenVT, S);
        U = DAG.getNode(ISD::TRUNCATE, DL, XLenVT, U);
        SDValue Lo = DAG.getNode(ISD::MUL, DL, XLenVT, S, U);
        SDValue Hi = DAG.getNode(VCSIRISD::MULHSU, DL, XLenVT, S, U);
        return DAG.getNode(ISD::BUILD_PAIR, DL, N->getValueType(0), Lo, Hi);
      };

      bool LHSIsS = DAG.ComputeNumSignBits(LHS) > XLen;
      bool RHSIsS = DAG.ComputeNumSignBits(RHS) > XLen;

      // The other operand should be signed, but still prefer MULH when
      // possible.
      if (RHSIsU && LHSIsS && !RHSIsS)
        Results.push_back(MakeMULPair(LHS, RHS));
      else if (LHSIsU && RHSIsS && !LHSIsS)
        Results.push_back(MakeMULPair(RHS, LHS));

      return;
    }
    [[fallthrough]];
  }
  case ISD::ADD:
  case ISD::SUB:
    break;
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    break;
  case ISD::ROTL:
  case ISD::ROTR:
    break;
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    return;
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::UREM:
    break;
  case ISD::SADDO:
  case ISD::UADDO:
  case ISD::USUBO: {
    return;
  }
  case ISD::UADDSAT:
  case ISD::USUBSAT: {
  case ISD::SADDSAT:
  case ISD::SSUBSAT: {
    return;
  }
  case ISD::ABS: {
    return;
  }
  case ISD::BITCAST: {
    break;
  }
  case ISD::EXTRACT_VECTOR_ELT: {
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN:
    break;
  }
  }
}

static Align getPrefTypeAlign(EVT VT, SelectionDAG &DAG) {
  return DAG.getDataLayout().getPrefTypeAlign(
      VT.getTypeForEVT(*DAG.getContext()));
}

// Convert Val to a ValVT. Should not be called for CCValAssign::Indirect
// values.
static SDValue convertLocVTToValVT(SelectionDAG &DAG, SDValue Val,
                                   const CCValAssign &VA, const SDLoc &DL,
                                   const VCSIRSubtarget &Subtarget) {
  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
    break;
  case CCValAssign::BCvt:
    Val = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), Val);
    break;
  }
  return Val;
}

// Lower a call to a callseq_start + CALL + callseq_end chain, and add input
// and output parameter nodes.
SDValue VCSIRTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  MVT XLenVT = Subtarget.getXLenVT();

  MachineFunction &MF = DAG.getMachineFunction();

  // Analyze the operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState ArgCCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  if (CallConv == CallingConv::GHC) {
    ArgCCInfo.AnalyzeCallOperands(Outs, CC_VCSIR_GHC);
  } else
    analyzeOutputArgs(MF, ArgCCInfo, Outs, /*IsRet=*/false, &CLI,
                      CallConv == CallingConv::Fast ? CC_VCSIR_FastCC
                                                    : CC_VCSIR);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = ArgCCInfo.getStackSize();

  // Create local copies for byval args
  SmallVector<SDValue, 8> ByValArgs;
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    if (!Flags.isByVal())
      continue;

    SDValue Arg = OutVals[i];
    unsigned Size = Flags.getByValSize();
    Align Alignment = Flags.getNonZeroByValAlign();

    int FI =
        MF.getFrameInfo().CreateStackObject(Size, Alignment, /*isSS=*/false);
    SDValue FIPtr = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    SDValue SizeNode = DAG.getConstant(Size, DL, XLenVT);

    Chain = DAG.getMemcpy(Chain, DL, FIPtr, Arg, SizeNode, Alignment,
                          /*IsVolatile=*/false,
                          /*AlwaysInline=*/false, /*CI*/ nullptr, IsTailCall,
                          MachinePointerInfo(), MachinePointerInfo());
    ByValArgs.push_back(FIPtr);
  }

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, CLI.DL);

  // Copy argument values to their designated locations.
  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;
  for (unsigned i = 0, j = 0, e = ArgLocs.size(), OutIdx = 0; i != e;
       ++i, ++OutIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue ArgValue = OutVals[OutIdx];
    ISD::ArgFlagsTy Flags = Outs[OutIdx].Flags;

    // Promote the value if needed.
    // For now, only handle fully promoted and indirect arguments.
    if (VA.getLocInfo() == CCValAssign::Indirect) {
      // Store the argument in a stack slot and pass its address.
      Align StackAlign =
          std::max(getPrefTypeAlign(Outs[OutIdx].ArgVT, DAG),
                   getPrefTypeAlign(ArgValue.getValueType(), DAG));
      TypeSize StoredSize = ArgValue.getValueType().getStoreSize();
      // If the original argument was split (e.g. i128), we need
      // to store the required parts of it here (and pass just one address).
      // Vectors may be partly split to registers and partly to the stack, in
      // which case the base address is partly offset and subsequent stores are
      // relative to that.
      unsigned ArgIndex = Outs[OutIdx].OrigArgIndex;
      unsigned ArgPartOffset = Outs[OutIdx].PartOffset;
      assert(VA.getValVT().isVector() || ArgPartOffset == 0);
      // Calculate the total size to store. We don't have access to what we're
      // actually storing other than performing the loop and collecting the
      // info.
      SmallVector<std::pair<SDValue, SDValue>> Parts;
      while (i + 1 != e && Outs[OutIdx + 1].OrigArgIndex == ArgIndex) {
        SDValue PartValue = OutVals[OutIdx + 1];
        unsigned PartOffset = Outs[OutIdx + 1].PartOffset - ArgPartOffset;
        SDValue Offset = DAG.getIntPtrConstant(PartOffset, DL);
        EVT PartVT = PartValue.getValueType();
        if (PartVT.isScalableVector())
          Offset = DAG.getNode(ISD::VSCALE, DL, XLenVT, Offset);
        StoredSize += PartVT.getStoreSize();
        StackAlign = std::max(StackAlign, getPrefTypeAlign(PartVT, DAG));
        Parts.push_back(std::make_pair(PartValue, Offset));
        ++i;
        ++OutIdx;
      }
      SDValue SpillSlot = DAG.CreateStackTemporary(StoredSize, StackAlign);
      int FI = cast<FrameIndexSDNode>(SpillSlot)->getIndex();
      MemOpChains.push_back(
          DAG.getStore(Chain, DL, ArgValue, SpillSlot,
                       MachinePointerInfo::getFixedStack(MF, FI)));
      for (const auto &Part : Parts) {
        SDValue PartValue = Part.first;
        SDValue PartOffset = Part.second;
        SDValue Address =
            DAG.getNode(ISD::ADD, DL, PtrVT, SpillSlot, PartOffset);
        MemOpChains.push_back(
            DAG.getStore(Chain, DL, PartValue, Address,
                         MachinePointerInfo::getFixedStack(MF, FI)));
      }
      ArgValue = SpillSlot;
    }

    // Use local copy if it is a byval arg.
    if (Flags.isByVal())
      ArgValue = ByValArgs[j++];

    if (VA.isRegLoc()) {
      // Queue up the argument copies and emit them at the end.
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), ArgValue));
    } else {
      assert(VA.isMemLoc() && "Argument not register or memory");
      assert(!IsTailCall && "Tail call not allowed if stack is used "
                            "for passing parameters");

      // Work out the address of the stack slot.
      if (!StackPtr.getNode())
        StackPtr = DAG.getCopyFromReg(Chain, DL, VCSIR::X2, PtrVT);
      SDValue Address =
          DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr,
                      DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));

      // Emit the store.
      MemOpChains.push_back(
          DAG.getStore(Chain, DL, ArgValue, Address,
                       MachinePointerInfo::getStack(MF, VA.getLocMemOffset())));
    }
  }

  // Join the stores, which are independent of one another.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue Glue;

  // Build a sequence of copy-to-reg nodes, chained and glued together.
  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg.first, Reg.second, Glue);
    Glue = Chain.getValue(1);
  }

  // Validate that none of the argument registers have been marked as
  // reserved, if so report an error. Do the same for the return address if this
  // is not a tailcall.
  validateCCReservedRegs(RegsToPass, MF);
  if (!IsTailCall && MF.getSubtarget().isRegisterReservedByUser(VCSIR::X1))
    MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
        MF.getFunction(),
        "Return address register required, but has been reserved."});

  if (GlobalAddressSDNode *S = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = S->getGlobal();
    Callee = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, VCSIRII::MO_CALL);
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee =
        DAG.getTargetExternalSymbol(S->getSymbol(), PtrVT, VCSIRII::MO_CALL);
  }

  // The first call operand is the chain and the second is the target address.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  // Glue the call to the argument copies, if any.
  if (Glue.getNode())
    Ops.push_back(Glue);

  assert((!CLI.CFIType || CLI.CB->isIndirectCall()) &&
         "Unexpected CFI type for a direct call");

  // Emit the call.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  unsigned CallOpc = VCSIRISD::CALL;
  Chain = DAG.getNode(CallOpc, DL, NodeTys, Ops);
  if (CLI.CFIType)
    Chain.getNode()->setCFIType(CLI.CFIType->getZExtValue());
  DAG.addNoMergeSiteInfo(Chain.getNode(), CLI.NoMerge);
  Glue = Chain.getValue(1);

  // Mark the end of the call, which is glued to the call itself.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RetCCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());
  analyzeInputArgs(MF, RetCCInfo, Ins, /*IsRet=*/true, CC_VCSIR);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    auto &VA = RVLocs[i];
    // Copy the value out
    SDValue RetValue =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    // Glue the RetValue to the end of the call sequence
    Chain = RetValue.getValue(1);
    Glue = RetValue.getValue(2);

    RetValue = convertLocVTToValVT(DAG, RetValue, VA, DL, Subtarget);

    InVals.push_back(RetValue);
  }

  return Chain;
}

SDValue VCSIRTargetLowering::PerformDAGCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  return SDValue();
}

bool VCSIRTargetLowering::shouldTransformSignedTruncationCheck(
    EVT XVT, unsigned KeptBits) const {
  // For vectors, we don't have a preference..
  if (XVT.isVector())
    return false;

  if (XVT != MVT::i32 && XVT != MVT::i64)
    return false;

  // We can use sext.w for RV64 or an srai 31 on RV32.
  if (KeptBits == 32 || KeptBits == 64)
    return true;

  // With Zbb we can use sext.h/sext.b.
  return false;
}
void VCSIRTargetLowering::analyzeInputArgs(
    MachineFunction &MF, CCState &CCInfo,
    const SmallVectorImpl<ISD::InputArg> &Ins, bool IsRet,
    VCSIRCCAssignFn Fn) const {
  unsigned NumArgs = Ins.size();
  FunctionType *FType = MF.getFunction().getFunctionType();

  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT ArgVT = Ins[i].VT;
    ISD::ArgFlagsTy ArgFlags = Ins[i].Flags;

    Type *ArgTy = nullptr;
    if (IsRet)
      ArgTy = FType->getReturnType();
    else if (Ins[i].isOrigArg())
      ArgTy = FType->getParamType(Ins[i].getOrigArgIndex());

    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, CCInfo,
           /*IsFixed=*/true, IsRet, ArgTy)) {
      LLVM_DEBUG(dbgs() << "InputArg #" << i << " has unhandled type " << ArgVT
                        << '\n');
      llvm_unreachable(nullptr);
    }
  }
}

bool VCSIRTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);

  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    MVT VT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    if (CC_VCSIR(i, VT, VT, CCValAssign::Full, ArgFlags, CCInfo,
                 /*IsFixed=*/true, /*IsRet=*/true, nullptr))
      return false;
  }
  return true;
}

bool VCSIRTargetLowering::isDesirableToCommuteWithShift(
    const SDNode *N, CombineLevel Level) const {
  assert((N->getOpcode() == ISD::SHL || N->getOpcode() == ISD::SRA ||
          N->getOpcode() == ISD::SRL) &&
         "Expected shift op");

  // The following folds are only desirable if `(OP _, c1 << c2)` can be
  // materialised in fewer instructions than `(OP _, c1)`:
  //
  //   (shl (add x, c1), c2) -> (add (shl x, c2), c1 << c2)
  //   (shl (or x, c1), c2) -> (or (shl x, c2), c1 << c2)
  SDValue N0 = N->getOperand(0);
  EVT Ty = N0.getValueType();

  // LD/ST will optimize constant Offset extraction, so when AddNode is used by
  // LD/ST, it can still complete the folding optimization operation performed
  // above.
  auto isUsedByLdSt = [](const SDNode *X, const SDNode *User) {
    for (SDNode *Use : X->users()) {
      // This use is the one we're on right now. Skip it
      if (Use == User || Use->getOpcode() == ISD::SELECT)
        continue;
      if (!isa<StoreSDNode>(Use) && !isa<LoadSDNode>(Use))
        return false;
    }
    return true;
  };

  return false;
}

bool VCSIRTargetLowering::targetShrinkDemandedConstant(
    SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
    TargetLoweringOpt &TLO) const {
  // Delay this optimization as late as possible.
  if (!TLO.LegalOps)
    return false;

  EVT VT = Op.getValueType();
  if (VT.isVector())
    return false;

  unsigned Opcode = Op.getOpcode();
  if (Opcode != ISD::AND && Opcode != ISD::OR && Opcode != ISD::XOR)
    return false;

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!C)
    return false;

  const APInt &Mask = C->getAPIntValue();

  // Clear all non-demanded bits initially.
  APInt ShrunkMask = Mask & DemandedBits;

  // Try to make a smaller immediate by setting undemanded bits.

  APInt ExpandedMask = Mask | ~DemandedBits;

  auto IsLegalMask = [ShrunkMask, ExpandedMask](const APInt &Mask) -> bool {
    return ShrunkMask.isSubsetOf(Mask) && Mask.isSubsetOf(ExpandedMask);
  };
  auto UseMask = [Mask, Op, &TLO](const APInt &NewMask) -> bool {
    if (NewMask == Mask)
      return true;
    SDLoc DL(Op);
    SDValue NewC = TLO.DAG.getConstant(NewMask, DL, Op.getValueType());
    SDValue NewOp = TLO.DAG.getNode(Op.getOpcode(), DL, Op.getValueType(),
                                    Op.getOperand(0), NewC);
    return TLO.CombineTo(Op, NewOp);
  };

  // If the shrunk mask fits in sign extended 12 bits, let the target
  // independent code apply it.
  if (ShrunkMask.isSignedIntN(12))
    return false;

  // And has a few special cases for zext.
  if (Opcode == ISD::AND) {
    // Preserve (and X, 0xffff), if zext.h exists use zext.h,
    // otherwise use SLLI + SRLI.
    APInt NewMask = APInt(Mask.getBitWidth(), 0xffff);
    if (IsLegalMask(NewMask))
      return UseMask(NewMask);

    // Try to preserve (and X, 0xffffffff), the (zext_inreg X, i32) pattern.
    if (VT == MVT::i64) {
      APInt NewMask = APInt(64, 0xffffffff);
      if (IsLegalMask(NewMask))
        return UseMask(NewMask);
    }
  }

  // For the remaining optimizations, we need to be able to make a negative
  // number through a combination of mask and undemanded bits.
  if (!ExpandedMask.isNegative())
    return false;

  // What is the fewest number of bits we need to represent the negative number.
  unsigned MinSignedBits = ExpandedMask.getSignificantBits();

  // Try to make a 12 bit negative immediate. If that fails try to make a 32
  // bit negative immediate unless the shrunk immediate already fits in 32 bits.
  // If we can't create a simm12, we shouldn't change opaque constants.
  APInt NewMask = ShrunkMask;
  if (MinSignedBits <= 12)
    NewMask.setBitsFrom(11);
  else if (!C->isOpaque() && MinSignedBits <= 32 &&
           !ShrunkMask.isSignedIntN(32))
    NewMask.setBitsFrom(31);
  else
    return false;

  // Check that our new mask is a subset of the demanded mask.
  assert(IsLegalMask(NewMask));
  return UseMask(NewMask);
}

void VCSIRTargetLowering::analyzeOutputArgs(
    MachineFunction &MF, CCState &CCInfo,
    const SmallVectorImpl<ISD::OutputArg> &Outs, bool IsRet,
    CallLoweringInfo *CLI, VCSIRCCAssignFn Fn) const {
  unsigned NumArgs = Outs.size();

  for (unsigned i = 0; i != NumArgs; i++) {
    MVT ArgVT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    Type *OrigTy = CLI ? CLI->getArgs()[Outs[i].OrigArgIndex].Ty : nullptr;

    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, CCInfo,
           Outs[i].IsFixed, IsRet, OrigTy)) {
      LLVM_DEBUG(dbgs() << "OutputArg #" << i << " has unhandled type " << ArgVT
                        << "\n");
      llvm_unreachable(nullptr);
    }
  }
}

static SDValue convertValVTToLocVT(SelectionDAG &DAG, SDValue Val,
                                   const CCValAssign &VA, const SDLoc &DL,
                                   const VCSIRSubtarget &Subtarget) {
  EVT LocVT = VA.getLocVT();

  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
    break;
  case CCValAssign::BCvt:
    Val = DAG.getNode(ISD::BITCAST, DL, LocVT, Val);
    break;
  }
  return Val;
}

SDValue
VCSIRTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const VCSIRSubtarget &STI = MF.getSubtarget<VCSIRSubtarget>();

  // Stores the assignment of the return value to a location.
  SmallVector<CCValAssign, 16> RVLocs;

  // Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  analyzeOutputArgs(DAG.getMachineFunction(), CCInfo, Outs, /*IsRet=*/true,
                    nullptr, CC_VCSIR);

  if (CallConv == CallingConv::GHC && !RVLocs.empty())
    report_fatal_error("GHC functions return void only");

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0, e = RVLocs.size(), OutIdx = 0; i < e; ++i, ++OutIdx) {
    SDValue Val = OutVals[OutIdx];
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    // Handle a 'normal' return.
    Val = convertValVTToLocVT(DAG, Val, VA, DL, Subtarget);
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);

    if (STI.isRegisterReservedByUser(VA.getLocReg()))
      MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
          MF.getFunction(),
          "Return value register required, but has been reserved."});

    // Guarantee that all emitted copies are stuck together.
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain; // Update chain.

  // Add the glue node if we have it.
  if (Glue.getNode()) {
    RetOps.push_back(Glue);
  }

  unsigned RetOpc = VCSIRISD::RET_GLUE;
  // Interrupt service routines use different return instructions.
  const Function &Func = DAG.getMachineFunction().getFunction();

  return DAG.getNode(RetOpc, DL, MVT::Other, RetOps);
}

unsigned VCSIRTargetLowering::getStackProbeSize(const MachineFunction &MF,
                                                Align StackAlign) const {
  // The default stack probe size is 4096 if the function has no
  // stack-probe-size attribute.
  const Function &Fn = MF.getFunction();
  unsigned StackProbeSize =
      Fn.getFnAttributeAsParsedInteger("stack-probe-size", 4096);
  // Round down to the stack alignment.
  StackProbeSize = alignDown(StackProbeSize, StackAlign.value());
  return StackProbeSize ? StackProbeSize : StackAlign.value();
}

void VCSIRTargetLowering::validateCCReservedRegs(
    const SmallVectorImpl<std::pair<llvm::Register, llvm::SDValue>> &Regs,
    MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  const VCSIRSubtarget &STI = MF.getSubtarget<VCSIRSubtarget>();

  if (llvm::any_of(Regs, [&STI](auto Reg) {
        return STI.isRegisterReservedByUser(Reg.first);
      }))
    F.getContext().diagnose(DiagnosticInfoUnsupported{
        F, "Argument register required, but has been reserved."});
}

// Check if the result of the node is only used as a return value, as
// otherwise we can't perform a tail-call.
bool VCSIRTargetLowering::isUsedByReturnOnly(SDNode *N, SDValue &Chain) const {
  if (N->getNumValues() != 1)
    return false;
  if (!N->hasNUsesOfValue(1, 0))
    return false;

  SDNode *Copy = *N->user_begin();

  if (Copy->getOpcode() == ISD::BITCAST) {
    return isUsedByReturnOnly(Copy, Chain);
  }

  // TODO: Handle additional opcodes in order to support tail-calling libcalls
  // with soft float ABIs.
  if (Copy->getOpcode() != ISD::CopyToReg) {
    return false;
  }

  // If the ISD::CopyToReg has a glue operand, we conservatively assume it
  // isn't safe to perform a tail call.
  if (Copy->getOperand(Copy->getNumOperands() - 1).getValueType() == MVT::Glue)
    return false;

  // The copy must be used by a VCSIRISD::RET_GLUE, and nothing else.
  bool HasRet = false;
  for (SDNode *Node : Copy->users()) {
    if (Node->getOpcode() != VCSIRISD::RET_GLUE)
      return false;
    HasRet = true;
  }
  if (!HasRet)
    return false;

  Chain = Copy->getOperand(0);
  return true;
}

bool VCSIRTargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  return CI->isTailCall();
}

const char *VCSIRTargetLowering::getTargetNodeName(unsigned Opcode) const {
#define NODE_NAME_CASE(NODE)                                                   \
  case VCSIRISD::NODE:                                                         \
    return "VCSIRISD::" #NODE;
  // clang-format off
  switch ((VCSIRISD::NodeType)Opcode) {
  case VCSIRISD::FIRST_NUMBER:
    break;
  NODE_NAME_CASE(RET_GLUE)
  NODE_NAME_CASE(CALL)
  NODE_NAME_CASE(BR_CC)
  NODE_NAME_CASE(ADD_LO)
  NODE_NAME_CASE(HI)
  NODE_NAME_CASE(MULHSU)
  }
  // clang-format on
  return nullptr;
#undef NODE_NAME_CASE
}

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
VCSIRTargetLowering::ConstraintType
VCSIRTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'f':
    case 'R':
      return C_RegisterClass;
    case 'I':
    case 'J':
    case 'K':
      return C_Immediate;
    case 'A':
      return C_Memory;
    case 's':
    case 'S': // A symbolic address
      return C_Other;
    }
  } else {
    if (Constraint == "vr" || Constraint == "vd" || Constraint == "vm")
      return C_RegisterClass;
    if (Constraint == "cr" || Constraint == "cR" || Constraint == "cf")
      return C_RegisterClass;
  }
  return TargetLowering::getConstraintType(Constraint);
}

InlineAsm::ConstraintCode
VCSIRTargetLowering::getInlineAsmMemConstraint(StringRef ConstraintCode) const {
  // Currently only support length 1 constraints.
  if (ConstraintCode.size() == 1) {
    switch (ConstraintCode[0]) {
    case 'A':
      return InlineAsm::ConstraintCode::A;
    default:
      break;
    }
  }

  return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
}

void VCSIRTargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, StringRef Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  // Currently only support length 1 constraints.
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'I':
      // Validate & create a 12-bit signed immediate operand.
      if (auto *C = dyn_cast<ConstantSDNode>(Op)) {
        uint64_t CVal = C->getSExtValue();
        if (isInt<12>(CVal))
          Ops.push_back(DAG.getSignedTargetConstant(CVal, SDLoc(Op), MVT::i32));
      }
      return;
    case 'J':
      // Validate & create an integer zero operand.
      if (isNullConstant(Op))
        Ops.push_back(DAG.getTargetConstant(0, SDLoc(Op), MVT::i32));
      return;
    case 'K':
      // Validate & create a 5-bit unsigned immediate operand.
      if (auto *C = dyn_cast<ConstantSDNode>(Op)) {
        uint64_t CVal = C->getZExtValue();
        if (isUInt<5>(CVal))
          Ops.push_back(DAG.getTargetConstant(CVal, SDLoc(Op), MVT::i32));
      }
      return;
    case 'S':
      TargetLowering::LowerAsmOperandForConstraint(Op, "s", Ops, DAG);
      return;
    default:
      break;
    }
  }
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

const MCExpr *VCSIRTargetLowering::LowerCustomJumpTableEntry(
    const MachineJumpTableInfo *MJTI, const MachineBasicBlock *MBB,
    unsigned uid, MCContext &Ctx) const {
  return MCSymbolRefExpr::create(MBB->getSymbol(), Ctx);
}

bool VCSIRTargetLowering::getIndexedAddressParts(SDNode *Op, SDValue &Base,
                                                 SDValue &Offset,
                                                 ISD::MemIndexedMode &AM,
                                                 SelectionDAG &DAG) const {
  if (Op->getOpcode() != ISD::ADD && Op->getOpcode() != ISD::SUB)
    return false;

  Base = Op->getOperand(0);
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1))) {
    int64_t RHSC = RHS->getSExtValue();
    if (Op->getOpcode() == ISD::SUB)
      RHSC = -(uint64_t)RHSC;

    // The constants that can be encoded in the THeadMemIdx instructions
    // are of the form (sign_extend(imm5) << imm2).
    bool isLegalIndexedOffset = false;
    for (unsigned i = 0; i < 4; i++)
      if (isInt<5>(RHSC >> i) && ((RHSC % (1LL << i)) == 0)) {
        isLegalIndexedOffset = true;
        break;
      }

    if (!isLegalIndexedOffset)
      return false;

    Offset = Op->getOperand(1);
    return true;
  }

  return false;
}

bool VCSIRTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                    SDValue &Offset,
                                                    ISD::MemIndexedMode &AM,
                                                    SelectionDAG &DAG) const {
  EVT VT;
  SDValue Ptr;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
  } else
    return false;

  if (!getIndexedAddressParts(Ptr.getNode(), Base, Offset, AM, DAG))
    return false;

  AM = ISD::PRE_INC;
  return true;
}

bool VCSIRTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                     SDValue &Base,
                                                     SDValue &Offset,
                                                     ISD::MemIndexedMode &AM,
                                                     SelectionDAG &DAG) const {
  EVT VT;
  SDValue Ptr;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
  } else
    return false;

  if (!getIndexedAddressParts(Op, Base, Offset, AM, DAG))
    return false;
  // Post-indexing updates the base, so it's not a valid transform
  // if that's not the same as the load's pointer.
  if (Ptr != Base)
    return false;

  AM = ISD::POST_INC;
  return true;
}

Register VCSIRTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return VCSIR::X10;
}

Register VCSIRTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return VCSIR::X11;
}

bool VCSIRTargetLowering::shouldExtendTypeInLibCall(EVT Type) const {
  return true;
}

bool VCSIRTargetLowering::shouldSignExtendTypeInLibCall(Type *Ty,
                                                        bool IsSigned) const {
  return IsSigned;
}

bool VCSIRTargetLowering::decomposeMulByConstant(LLVMContext &Context, EVT VT,
                                                 SDValue C) const {
  // Check integral scalar types.
  if (!VT.isScalarInteger())
    return false;

  // Omit the optimization if the sub target has the M extension and the data
  // size exceeds XLen.

  auto *ConstNode = cast<ConstantSDNode>(C);
  const APInt &Imm = ConstNode->getAPIntValue();

  // Break the MUL to a SLLI and an ADD/SUB.
  if ((Imm + 1).isPowerOf2() || (Imm - 1).isPowerOf2() ||
      (1 - Imm).isPowerOf2() || (-1 - Imm).isPowerOf2())
    return true;

  // Break the MUL to two SLLI instructions and an ADD/SUB, if Imm needs
  // a pair of LUI/ADDI.
  if (!Imm.isSignedIntN(12) && Imm.countr_zero() < 12 &&
      ConstNode->hasOneUse()) {
    APInt ImmS = Imm.ashr(Imm.countr_zero());
    if ((ImmS + 1).isPowerOf2() || (ImmS - 1).isPowerOf2() ||
        (1 - ImmS).isPowerOf2())
      return true;
  }

  return false;
}

bool VCSIRTargetLowering::isMulAddWithConstProfitable(SDValue AddNode,
                                                      SDValue ConstNode) const {
  // Let the DAGCombiner decide for vectors.
  EVT VT = AddNode.getValueType();
  if (VT.isVector())
    return true;

  // Let the DAGCombiner decide for larger types.
  if (VT.getScalarSizeInBits() > 32)
    return true;

  // It is worse if c1 is simm12 while c1*c2 is not.
  ConstantSDNode *C1Node = cast<ConstantSDNode>(AddNode.getOperand(1));
  ConstantSDNode *C2Node = cast<ConstantSDNode>(ConstNode);
  const APInt &C1 = C1Node->getAPIntValue();
  const APInt &C2 = C2Node->getAPIntValue();
  if (C1.isSignedIntN(12) && !(C1 * C2).isSignedIntN(12))
    return false;

  // Default to true and let the DAGCombiner decide.
  return true;
}

EVT VCSIRTargetLowering::getOptimalMemOpType(
    const MemOp &Op, const AttributeList &FuncAttributes) const {
  return MVT::Other;
}

static Value *useTpOffset(IRBuilderBase &IRB, unsigned Offset) {
  Module *M = IRB.GetInsertBlock()->getModule();
  Function *ThreadPointerFunc =
      Intrinsic::getOrInsertDeclaration(M, Intrinsic::thread_pointer);
  return IRB.CreateConstGEP1_32(IRB.getInt8Ty(),
                                IRB.CreateCall(ThreadPointerFunc), Offset);
}

Value *VCSIRTargetLowering::getIRStackGuard(IRBuilderBase &IRB) const {
  Module *M = IRB.GetInsertBlock()->getModule();

  if (M->getStackProtectorGuard() == "tls") {
    // Users must specify the offset explicitly
    int Offset = M->getStackProtectorGuardOffset();
    return useTpOffset(IRB, Offset);
  }

  return TargetLowering::getIRStackGuard(IRB);
}

bool VCSIRTargetLowering::isLegalInterleavedAccessType(
    VectorType *VTy, unsigned Factor, Align Alignment, unsigned AddrSpace,
    const DataLayout &DL) const {
  return false;
}

bool VCSIRTargetLowering::isLegalStridedLoadStore(EVT DataType,
                                                  Align Alignment) const {
  return false;
}
static SDValue getTargetNode(GlobalAddressSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetGlobalAddress(N->getGlobal(), DL, Ty, 0, Flags);
}

static SDValue getTargetNode(BlockAddressSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetBlockAddress(N->getBlockAddress(), Ty, N->getOffset(),
                                   Flags);
}

static SDValue getTargetNode(ConstantPoolSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetConstantPool(N->getConstVal(), Ty, N->getAlign(),
                                   N->getOffset(), Flags);
}

static SDValue getTargetNode(JumpTableSDNode *N, const SDLoc &DL, EVT Ty,
                             SelectionDAG &DAG, unsigned Flags) {
  return DAG.getTargetJumpTable(N->getIndex(), Ty, Flags);
}
template <class NodeTy>
SDValue VCSIRTargetLowering::getAddr(NodeTy *N, SelectionDAG &DAG, bool IsLocal,
                                     bool IsExternWeak) const {
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());

  switch (getTargetMachine().getCodeModel()) {
  default:
    report_fatal_error("Unsupported code model for lowering");
  case CodeModel::Small: {
    // Generate a sequence for accessing addresses within the first 2 GiB of
    // address space. This generates the pattern (addi (lui %hi(sym)) %lo(sym)).
    SDValue AddrHi = getTargetNode(N, DL, Ty, DAG, VCSIRII::MO_HI);
    SDValue AddrLo = getTargetNode(N, DL, Ty, DAG, VCSIRII::MO_LO);
    SDValue MNHi = DAG.getNode(VCSIRISD::HI, DL, Ty, AddrHi);
    return DAG.getNode(VCSIRISD::ADD_LO, DL, Ty, MNHi, AddrLo);
  }
  }
}

#define GET_REGISTER_MATCHER
#include "VCSIRGenAsmMatcher.inc"

Register
VCSIRTargetLowering::getRegisterByName(const char *RegName, LLT VT,
                                       const MachineFunction &MF) const {
  auto Reg = MatchRegisterName(RegName);
  if (Reg == VCSIR::NoRegister)
    report_fatal_error(
        Twine("Invalid register name \"" + StringRef(RegName) + "\"."));
  BitVector ReservedRegs = Subtarget.getRegisterInfo()->getReservedRegs(MF);
  if (!ReservedRegs.test(Reg) && !Subtarget.isRegisterReservedByUser(Reg))
    report_fatal_error(Twine("Trying to obtain non-reserved register \"" +
                             StringRef(RegName) + "\"."));
  return Reg;
}

// The caller is responsible for loading the full value if the argument is
// passed with CCValAssign::Indirect.
static SDValue unpackFromRegLoc(SelectionDAG &DAG, SDValue Chain,
                                const CCValAssign &VA, const SDLoc &DL,
                                const ISD::InputArg &In,
                                const VCSIRTargetLowering &TLI) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  EVT LocVT = VA.getLocVT();
  SDValue Val;
  const TargetRegisterClass *RC = TLI.getRegClassFor(LocVT.getSimpleVT());
  Register VReg = RegInfo.createVirtualRegister(RC);
  RegInfo.addLiveIn(VA.getLocReg(), VReg);
  Val = DAG.getCopyFromReg(Chain, DL, VReg, LocVT);

  // If input is sign extended from 32 bits, note it for the SExtWRemoval pass.
  if (In.isOrigArg()) {
    Argument *OrigArg = MF.getFunction().getArg(In.getOrigArgIndex());
    if (OrigArg->getType()->isIntegerTy()) {
      unsigned BitWidth = OrigArg->getType()->getIntegerBitWidth();
      // An input zero extended from i31 can also be considered sign extended.
      if ((BitWidth <= 32 && In.Flags.isSExt()) ||
          (BitWidth < 32 && In.Flags.isZExt())) {
        VCSIRMachineFunctionInfo *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();
        RVFI->addSExt32Register(VReg);
      }
    }
  }

  if (VA.getLocInfo() == CCValAssign::Indirect)
    return Val;

  return convertLocVTToValVT(DAG, Val, VA, DL, TLI.getSubtarget());
}

// The caller is responsible for loading the full value if the argument is
// passed with CCValAssign::Indirect.
static SDValue unpackFromMemLoc(SelectionDAG &DAG, SDValue Chain,
                                const CCValAssign &VA, const SDLoc &DL) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  EVT LocVT = VA.getLocVT();
  EVT ValVT = VA.getValVT();
  EVT PtrVT = MVT::getIntegerVT(DAG.getDataLayout().getPointerSizeInBits(0));
  if (VA.getLocInfo() == CCValAssign::Indirect) {
    // When the value is a scalable vector, we save the pointer which points to
    // the scalable vector value in the stack. The ValVT will be the pointer
    // type, instead of the scalable vector type.
    ValVT = LocVT;
  }
  int FI = MFI.CreateFixedObject(ValVT.getStoreSize(), VA.getLocMemOffset(),
                                 /*IsImmutable=*/true);
  SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
  SDValue Val;

  ISD::LoadExtType ExtType = ISD::NON_EXTLOAD;
  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
  case CCValAssign::Indirect:
  case CCValAssign::BCvt:
    break;
  }
  Val = DAG.getExtLoad(
      ExtType, DL, LocVT, Chain, FIN,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI), ValVT);
  return Val;
}

// Transform physical registers into virtual registers.
SDValue VCSIRTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  MachineFunction &MF = DAG.getMachineFunction();

  const Function &Func = MF.getFunction();
  if (Func.hasFnAttribute("interrupt")) {
    if (!Func.arg_empty())
      report_fatal_error(
          "Functions with the interrupt attribute cannot have arguments!");

    StringRef Kind =
        MF.getFunction().getFnAttribute("interrupt").getValueAsString();

    if (!(Kind == "user" || Kind == "supervisor" || Kind == "machine"))
      report_fatal_error(
          "Function interrupt attribute argument not supported!");
  }

  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  MVT XLenVT = Subtarget.getXLenVT();
  unsigned XLenInBytes = Subtarget.getXLen() / 8;
  // Used with vargs to accumulate store chains.
  std::vector<SDValue> OutChains;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  if (CallConv == CallingConv::GHC)
    CCInfo.AnalyzeFormalArguments(Ins, CC_VCSIR_GHC);
  else
    analyzeInputArgs(MF, CCInfo, Ins, /*IsRet=*/false,
                     CallConv == CallingConv::Fast ? CC_VCSIR_FastCC
                                                   : CC_VCSIR);

  for (unsigned i = 0, e = ArgLocs.size(), InsIdx = 0; i != e; ++i, ++InsIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue ArgValue;
    if (VA.isRegLoc())
      ArgValue = unpackFromRegLoc(DAG, Chain, VA, DL, Ins[InsIdx], *this);
    else
      ArgValue = unpackFromMemLoc(DAG, Chain, VA, DL);

    if (VA.getLocInfo() == CCValAssign::Indirect) {
      // If the original argument was split and passed by reference (e.g. i128
      // on RV32), we need to load all parts of it here (using the same
      // address). Vectors may be partly split to registers and partly to the
      // stack, in which case the base address is partly offset and subsequent
      // stores are relative to that.
      InVals.push_back(DAG.getLoad(VA.getValVT(), DL, Chain, ArgValue,
                                   MachinePointerInfo()));
      unsigned ArgIndex = Ins[InsIdx].OrigArgIndex;
      unsigned ArgPartOffset = Ins[InsIdx].PartOffset;
      assert(VA.getValVT().isVector() || ArgPartOffset == 0);
      while (i + 1 != e && Ins[InsIdx + 1].OrigArgIndex == ArgIndex) {
        CCValAssign &PartVA = ArgLocs[i + 1];
        unsigned PartOffset = Ins[InsIdx + 1].PartOffset - ArgPartOffset;
        SDValue Offset = DAG.getIntPtrConstant(PartOffset, DL);
        if (PartVA.getValVT().isScalableVector())
          Offset = DAG.getNode(ISD::VSCALE, DL, XLenVT, Offset);
        SDValue Address = DAG.getNode(ISD::ADD, DL, PtrVT, ArgValue, Offset);
        InVals.push_back(DAG.getLoad(PartVA.getValVT(), DL, Chain, Address,
                                     MachinePointerInfo()));
        ++i;
        ++InsIdx;
      }
      continue;
    }
    InVals.push_back(ArgValue);
  }

  if (any_of(ArgLocs,
             [](CCValAssign &VA) { return VA.getLocVT().isScalableVector(); }))
    MF.getInfo<VCSIRMachineFunctionInfo>()->setIsVectorCall();

  if (IsVarArg) {
    ArrayRef<MCPhysReg> ArgRegs = VCSIR::getArgGPRs();
    unsigned Idx = CCInfo.getFirstUnallocated(ArgRegs);
    const TargetRegisterClass *RC = &VCSIR::GPRRegClass;
    MachineFrameInfo &MFI = MF.getFrameInfo();
    MachineRegisterInfo &RegInfo = MF.getRegInfo();
    VCSIRMachineFunctionInfo *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();

    // Size of the vararg save area. For now, the varargs save area is either
    // zero or large enough to hold a0-a7.
    int VarArgsSaveSize = XLenInBytes * (ArgRegs.size() - Idx);
    int FI;

    // If all registers are allocated, then all varargs must be passed on the
    // stack and we don't need to save any argregs.
    if (VarArgsSaveSize == 0) {
      int VaArgOffset = CCInfo.getStackSize();
      FI = MFI.CreateFixedObject(XLenInBytes, VaArgOffset, true);
    } else {
      int VaArgOffset = -VarArgsSaveSize;
      FI = MFI.CreateFixedObject(VarArgsSaveSize, VaArgOffset, true);

      // If saving an odd number of registers then create an extra stack slot to
      // ensure that the frame pointer is 2*XLEN-aligned, which in turn ensures
      // offsets to even-numbered registered remain 2*XLEN-aligned.
      if (Idx % 2) {
        MFI.CreateFixedObject(
            XLenInBytes, VaArgOffset - static_cast<int>(XLenInBytes), true);
        VarArgsSaveSize += XLenInBytes;
      }

      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);

      // Copy the integer registers that may have been used for passing varargs
      // to the vararg save area.
      for (unsigned I = Idx; I < ArgRegs.size(); ++I) {
        const Register Reg = RegInfo.createVirtualRegister(RC);
        RegInfo.addLiveIn(ArgRegs[I], Reg);
        SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, XLenVT);
        SDValue Store = DAG.getStore(
            Chain, DL, ArgValue, FIN,
            MachinePointerInfo::getFixedStack(MF, FI, (I - Idx) * XLenInBytes));
        OutChains.push_back(Store);
        FIN =
            DAG.getMemBasePlusOffset(FIN, TypeSize::getFixed(XLenInBytes), DL);
      }
    }

    // Record the frame index of the first variable argument
    // which is a value necessary to VASTART.
    RVFI->setVarArgsFrameIndex(FI);
    RVFI->setVarArgsSaveSize(VarArgsSaveSize);
  }

  // All stores are grouped in one node to allow the matching between
  // the size of Ins and InVals. This only happens for vararg functions.
  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  }

  return Chain;
}
// If an output pattern produces multiple instructions tablegen may pick an
// arbitrary type from an instructions destination register class to use for the
// VT of that MachineSDNode. This VT may be used to look up the representative
// register class. If the type isn't legal, the default implementation will
// not find a register class.
//
// Some integer types smaller than XLen are listed in the GPR register class to
// support isel patterns for GISel, but are not legal in SelectionDAG. The
// arbitrary type tablegen picks may be one of these smaller types.
//
// f16 and bf16 are both valid for the FPR16 or GPRF16 register class. It's
// possible for tablegen to pick bf16 as the arbitrary type for an f16 pattern.
std::pair<const TargetRegisterClass *, uint8_t>
VCSIRTargetLowering::findRepresentativeClass(const TargetRegisterInfo *TRI,
                                             MVT VT) const {
  switch (VT.SimpleTy) {
  default:
    break;
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
    return TargetLowering::findRepresentativeClass(TRI, MVT::i32);
    return TargetLowering::findRepresentativeClass(TRI, VT);
  }
}
