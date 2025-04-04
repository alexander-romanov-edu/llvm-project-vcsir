//===-- VCSIRISelLowering.h - VCSIR DAG Lowering Interface ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRISELLOWERING_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRISELLOWERING_H

#include "VCSIRCallingConv.h"

#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Support/InstructionCost.h"

namespace llvm {

class VCSIRSubtarget;
class VCSIRTargetMachine;

namespace VCSIRISD {

enum NodeType : unsigned {
  // Start the numbering where the builtin ops and target ops leave off.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET,
  CALL,
  SELECT_CC,
  BR_CC,
  ADD_LO,
  HI,
  RET_GLUE,
  MULHSU,
};

} // namespace VCSIRISD
class VCSIRTargetLowering : public TargetLowering {
  const VCSIRSubtarget &Subtarget;

public:
  explicit VCSIRTargetLowering(const TargetMachine &TM,
                               const VCSIRSubtarget &STI);

  const VCSIRSubtarget &getSubtarget() const { return Subtarget; }

  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;
  bool isLegalICmpImmediate(int64_t Imm) const override;
  bool isLegalAddImmediate(int64_t Imm) const override;
  bool isTruncateFree(Type *SrcTy, Type *DstTy) const override;
  bool isTruncateFree(EVT SrcVT, EVT DstVT) const override;
  bool isTruncateFree(SDValue Val, EVT VT2) const override;
  bool softPromoteHalfType() const override { return true; }

  /// Return the number of registers for a given MVT, for inline assembly
  unsigned
  getNumRegisters(LLVMContext &Context, EVT VT,
                  std::optional<MVT> RegisterVT = std::nullopt) const override;

  bool isMultiStoresCheaperThanBitsMerge(EVT LTy, EVT HTy) const override {
    // If the pair to store is a mixture of float and int values, we will
    // save two bitwise instructions and one float-to-int instruction and
    // increase one store instruction. There is potentially a more
    // significant benefit because it avoids the float->int domain switch
    // for input value. So It is more likely a win.
    if ((LTy.isFloatingPoint() && HTy.isInteger()) ||
        (LTy.isInteger() && HTy.isFloatingPoint()))
      return true;
    // If the pair only contains int values, we will save two bitwise
    // instructions and increase one store instruction (costing one more
    // store buffer). Since the benefit is more blurred we leave such a pair
    // out until we get testcase to prove it is a win.
    return false;
  }

  bool shouldExpandCttzElements(EVT VT) const override;

  // Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  bool targetShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                                    const APInt &DemandedElts,
                                    TargetLoweringOpt &TLO) const override;

  // This method returns the name of a target specific DAG node.
  const char *getTargetNodeName(unsigned Opcode) const override;

  ConstraintType getConstraintType(StringRef Constraint) const override;

  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override;

  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                            bool MathUsed) const override {
    if (VT == MVT::i8 || VT == MVT::i16)
      return false;

    return TargetLowering::shouldFormOverflowOp(Opcode, VT, MathUsed);
  }

  bool storeOfVectorConstantIsCheap(bool IsZero, EVT MemVT, unsigned NumElem,
                                    unsigned AddrSpace) const override {
    // If we can replace 4 or more scalar stores, there will be a reduction
    // in instructions even after we add a vector constant load.
    return NumElem >= 4;
  }

  unsigned getStackProbeSize(const MachineFunction &MF, Align StackAlign) const;
  bool convertSetCCLogicToBitwiseLogic(EVT VT) const override {
    return VT.isScalarInteger();
  }
  bool convertSelectOfConstantsToMath(EVT VT) const override { return true; }

  bool preferZeroCompareBranch() const override { return true; }

  bool shouldTransformSignedTruncationCheck(EVT XVT,
                                            unsigned KeptBits) const override;

  TargetLowering::ShiftLegalizationStrategy
  preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                     unsigned ExpansionFactor) const override {
    if (DAG.getMachineFunction().getFunction().hasMinSize())
      return ShiftLegalizationStrategy::LowerToLibcall;
    return TargetLowering::preferredShiftLegalizationStrategy(DAG, N,
                                                              ExpansionFactor);
  }

  bool isDesirableToCommuteWithShift(const SDNode *N,
                                     CombineLevel Level) const override;

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const override;

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override;

  bool shouldExtendTypeInLibCall(EVT Type) const override;
  bool shouldSignExtendTypeInLibCall(Type *Ty, bool IsSigned) const override;

  /// Returns the register with the specified architectural or ABI name. This
  /// method is necessary to lower the llvm.read_register.* and
  /// llvm.write_register.* intrinsics. Allocatable registers must be reserved
  /// with the clang -ffixed-xX flag for access to be allowed.
  Register getRegisterByName(const char *RegName, LLT VT,
                             const MachineFunction &MF) const override;

  // Lower incoming arguments, copy physregs into vregs
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;
  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const override;
  bool mayBeEmittedAsTailCall(const CallInst *CI) const override;
  bool shouldConsiderGEPOffsetSplit() const override { return true; }

  bool decomposeMulByConstant(LLVMContext &Context, EVT VT,
                              SDValue C) const override;

  bool isMulAddWithConstProfitable(SDValue AddNode,
                                   SDValue ConstNode) const override;

  EVT getOptimalMemOpType(const MemOp &Op,
                          const AttributeList &FuncAttributes) const override;

  const MCExpr *LowerCustomJumpTableEntry(const MachineJumpTableInfo *MJTI,
                                          const MachineBasicBlock *MBB,
                                          unsigned uid,
                                          MCContext &Ctx) const override;

  bool getIndexedAddressParts(SDNode *Op, SDValue &Base, SDValue &Offset,
                              ISD::MemIndexedMode &AM, SelectionDAG &DAG) const;
  bool getPreIndexedAddressParts(SDNode *N, SDValue &Base, SDValue &Offset,
                                 ISD::MemIndexedMode &AM,
                                 SelectionDAG &DAG) const override;
  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;

  bool isLegalScaleForGatherScatter(uint64_t Scale,
                                    uint64_t ElemSize) const override {
    // Scaled addressing not supported on indexed load/stores
    return Scale == 1;
  }

  /// If the target has a standard location for the stack protector cookie,
  /// returns the address of that location. Otherwise, returns nullptr.
  Value *getIRStackGuard(IRBuilderBase &IRB) const override;

  /// Returns whether or not generating a interleaved load/store intrinsic for
  /// this type will be legal.
  bool isLegalInterleavedAccessType(VectorType *VTy, unsigned Factor,
                                    Align Alignment, unsigned AddrSpace,
                                    const DataLayout &) const;

  /// Return true if a stride load store of the given result type and
  /// alignment is legal.
  bool isLegalStridedLoadStore(EVT DataType, Align Alignment) const;

  unsigned getMaxSupportedInterleaveFactor() const override { return 8; }

  bool supportKCFIBundles() const override { return false; }

private:
  void analyzeInputArgs(MachineFunction &MF, CCState &CCInfo,
                        const SmallVectorImpl<ISD::InputArg> &Ins, bool IsRet,
                        VCSIRCCAssignFn Fn) const;
  void analyzeOutputArgs(MachineFunction &MF, CCState &CCInfo,
                         const SmallVectorImpl<ISD::OutputArg> &Outs,
                         bool IsRet, CallLoweringInfo *CLI,
                         VCSIRCCAssignFn Fn) const;

  template <class NodeTy>
  SDValue getAddr(NodeTy *N, SelectionDAG &DAG, bool IsLocal = true,
                  bool IsExternWeak = false) const;
  SDValue getStaticTLSAddr(GlobalAddressSDNode *N, SelectionDAG &DAG,
                           bool UseGOT) const;
  SDValue getDynamicTLSAddr(GlobalAddressSDNode *N, SelectionDAG &DAG) const;
  SDValue getTLSDescAddr(GlobalAddressSDNode *N, SelectionDAG &DAG) const;

  SDValue lowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSELECT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerMaskedLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerMaskedStore(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVectorCompress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerMaskedGather(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerMaskedScatter(SDValue Op, SelectionDAG &DAG) const;

  bool isEligibleForTailCallOptimization(
      CCState &CCInfo, CallLoweringInfo &CLI, MachineFunction &MF,
      const SmallVector<CCValAssign, 16> &ArgLocs) const;

  /// Generate error diagnostics if any register used by CC has been marked
  /// reserved.
  void validateCCReservedRegs(
      const SmallVectorImpl<std::pair<llvm::Register, llvm::SDValue>> &Regs,
      MachineFunction &MF) const;

  /// Disable normalizing
  /// select(N0&N1, X, Y) => select(N0, select(N1, X, Y), Y) and
  /// select(N0|N1, X, Y) => select(N0, select(N1, X, Y, Y))
  /// VCSIR doesn't have flags so it's better to perform the and/or in a GPR.
  bool shouldNormalizeToSelectSequence(LLVMContext &, EVT) const override {
    return false;
  }

  std::pair<const TargetRegisterClass *, uint8_t>
  findRepresentativeClass(const TargetRegisterInfo *TRI, MVT VT) const override;
  bool isZExtFree(SDValue Val, EVT VT2) const override;
};

namespace VCSIRVIntrinsicsTable {

struct VCSIRVIntrinsicInfo {
  unsigned IntrinsicID;
  uint8_t ScalarOperand;
  uint8_t VLOperand;
  bool hasScalarOperand() const {
    // 0xF is not valid. See NoScalarOperand in IntrinsicsVCSIR.td.
    return ScalarOperand != 0xF;
  }
  bool hasVLOperand() const {
    // 0x1F is not valid. See NoVLOperand in IntrinsicsVCSIR.td.
    return VLOperand != 0x1F;
  }
};

using namespace VCSIR;

// #define GET_VCSIRVIntrinsicsTable_DECL
// #include "VCSIRGenSearchableTables.inc"
// #undef GET_VCSIRVIntrinsicsTable_DECL

} // end namespace VCSIRVIntrinsicsTable

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VCSIR_VCSIRISELLOWERING_H
