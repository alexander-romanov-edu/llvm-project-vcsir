//===- VCSIRTargetTransformInfo.h - VCSIR specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines a TargetTransformInfo::Concept conforming object specific
/// to the VCSIR target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRTARGETTRANSFORMINFO_H

#include "VCSIRSubtarget.h"
#include "VCSIRTargetMachine.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"
#include <optional>

namespace llvm {

class VCSIRTTIImpl : public BasicTTIImplBase<VCSIRTTIImpl> {
  using BaseT = BasicTTIImplBase<VCSIRTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const VCSIRSubtarget *ST;
  const VCSIRTargetLowering *TLI;

  const VCSIRSubtarget *getST() const { return ST; }
  const VCSIRTargetLowering *getTLI() const { return TLI; }

  /// This function calculates the costs for one or more RVV opcodes based
  /// on the vtype and the cost kind.
  /// \param Opcodes A list of opcodes of the RVV instruction to evaluate.
  /// \param VT The MVT of vtype associated with the RVV instructions.
  /// For widening/narrowing instructions where the result and source types
  /// differ, it is important to check the spec to determine whether the vtype
  /// refers to the result or source type.
  /// \param CostKind The type of cost to compute.
  InstructionCost getVCSIRInstructionCost(ArrayRef<unsigned> OpCodes, MVT VT,
                                          TTI::TargetCostKind CostKind);

  /// Return the cost of accessing a constant pool entry of the specified
  /// type.
  InstructionCost getConstantPoolLoadCost(Type *Ty,
                                          TTI::TargetCostKind CostKind);

public:
  explicit VCSIRTTIImpl(const VCSIRTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  /// Return the cost of materializing an immediate for a value operand of
  /// a store instruction.
  InstructionCost getStoreImmCost(Type *VecTy, TTI::OperandValueInfo OpInfo,
                                  TTI::TargetCostKind CostKind);

  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind);
  InstructionCost getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                    const APInt &Imm, Type *Ty,
                                    TTI::TargetCostKind CostKind,
                                    Instruction *Inst = nullptr);
  InstructionCost getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                      const APInt &Imm, Type *Ty,
                                      TTI::TargetCostKind CostKind);

  bool shouldExpandReduction(const IntrinsicInst *II) const;
  bool enableOrderedReductions() const { return true; }
  TailFoldingStyle
  getPreferredTailFoldingStyle(bool IVUpdateMayOverflow) const {
    return TailFoldingStyle::DataWithoutLaneMask;
  }
  std::optional<unsigned> getMaxVScale() const;
  std::optional<unsigned> getVScaleForTuning() const;

  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const;

  unsigned getRegUsageForType(Type *Ty) const;

  bool preferEpilogueVectorization() const {
    // Epilogue vectorization is usually unprofitable - tail folding or
    // a smaller VF would have been better.  This a blunt hammer - we
    // should re-examine this once vectorization is better tuned.
    return false;
  }

  InstructionCost getMaskedMemoryOpCost(unsigned Opcode, Type *Src,
                                        Align Alignment, unsigned AddressSpace,
                                        TTI::TargetCostKind CostKind);

  InstructionCost getPointersChainCost(ArrayRef<const Value *> Ptrs,
                                       const Value *Base,
                                       const TTI::PointersChainInfo &Info,
                                       Type *AccessTy,
                                       TTI::TargetCostKind CostKind);

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  InstructionCost getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                        TTI::TargetCostKind CostKind);

  InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond = false, bool UseMaskForGaps = false);

  InstructionCost getGatherScatterOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I);

  InstructionCost getStridedMemoryOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I);

  InstructionCost getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys);

  InstructionCost getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                   TTI::CastContextHint CCH,
                                   TTI::TargetCostKind CostKind,
                                   const Instruction *I = nullptr);

  InstructionCost getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                         FastMathFlags FMF,
                                         TTI::TargetCostKind CostKind);

  InstructionCost getArithmeticReductionCost(unsigned Opcode, VectorType *Ty,
                                             std::optional<FastMathFlags> FMF,
                                             TTI::TargetCostKind CostKind);

  InstructionCost getExtendedReductionCost(unsigned Opcode, bool IsUnsigned,
                                           Type *ResTy, VectorType *ValTy,
                                           FastMathFlags FMF,
                                           TTI::TargetCostKind CostKind);

  InstructionCost getMemoryOpCost(
      unsigned Opcode, Type *Src, MaybeAlign Alignment, unsigned AddressSpace,
      TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo OpdInfo = {TTI::OK_AnyValue, TTI::OP_None},
      const Instruction *I = nullptr);

  InstructionCost getCmpSelInstrCost(
      unsigned Opcode, Type *ValTy, Type *CondTy, CmpInst::Predicate VecPred,
      TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      const Instruction *I = nullptr);

  InstructionCost getCFInstrCost(unsigned Opcode, TTI::TargetCostKind CostKind,
                                 const Instruction *I = nullptr);

  using BaseT::getVectorInstrCost;

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = {}, const Instruction *CxtI = nullptr);

  bool isLegalMaskedLoadStore(Type *DataType, Align Alignment) { return false; }

  bool isLegalMaskedLoad(Type *DataType, Align Alignment) {
    return isLegalMaskedLoadStore(DataType, Alignment);
  }
  bool isLegalMaskedStore(Type *DataType, Align Alignment) {
    return isLegalMaskedLoadStore(DataType, Alignment);
  }

  bool isLegalMaskedGatherScatter(Type *DataType, Align Alignment) {
    return false;
  }

  bool isLegalMaskedGather(Type *DataType, Align Alignment) {
    return isLegalMaskedGatherScatter(DataType, Alignment);
  }
  bool isLegalMaskedScatter(Type *DataType, Align Alignment) {
    return isLegalMaskedGatherScatter(DataType, Alignment);
  }

  bool forceScalarizeMaskedGather(VectorType *VTy, Align Alignment) {
    return false;
  }

  bool forceScalarizeMaskedScatter(VectorType *VTy, Align Alignment) {
    // Scalarize masked scatter for RV64 if EEW=64 indices aren't supported.
    return false;
  }

  bool isLegalStridedLoadStore(Type *DataType, Align Alignment) {
    EVT DataTypeVT = TLI->getValueType(DL, DataType);
    return TLI->isLegalStridedLoadStore(DataTypeVT, Alignment);
  }

  bool isLegalInterleavedAccessType(VectorType *VTy, unsigned Factor,
                                    Align Alignment, unsigned AddrSpace) {
    return TLI->isLegalInterleavedAccessType(VTy, Factor, Alignment, AddrSpace,
                                             DL);
  }

  bool isLegalMaskedExpandLoad(Type *DataType, Align Alignment);

  bool isLegalMaskedCompressStore(Type *DataTy, Align Alignment);

  bool isVScaleKnownToBeAPowerOfTwo() const {
    return TLI->isVScaleKnownToBeAPowerOfTwo();
  }

  /// \returns How the target needs this vector-predicated operation to be
  /// transformed.
  TargetTransformInfo::VPLegalization
  getVPLegalizationStrategy(const VPIntrinsic &PI) const {
    using VPLegalization = TargetTransformInfo::VPLegalization;
    return VPLegalization(VPLegalization::Discard, VPLegalization::Convert);
  }

  bool isLegalToVectorizeReduction(const RecurrenceDescriptor &RdxDesc,
                                   ElementCount VF) const {
    return false;
  }

  unsigned getMaxInterleaveFactor(ElementCount VF) {
    // Don't interleave if the loop has been vectorized with scalable vectors.
    if (VF.isScalable())
      return 1;
    // If the loop will not be vectorized, don't interleave the loop.
    // Let regular unroll to unroll the loop.
    return 1;
  }

  bool enableInterleavedAccessVectorization() { return true; }

  unsigned getNumberOfRegisters(unsigned) const { return 32; }

  TTI::AddressingModeKind getPreferredAddressingMode(const Loop *L,
                                                     ScalarEvolution *SE) const;

  bool isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                     const TargetTransformInfo::LSRCost &C2);

  bool
  shouldConsiderAddressTypePromotion(const Instruction &I,
                                     bool &AllowPromotionWithoutCommonHeader);
  std::optional<unsigned> getMinPageSize() const { return 4096; }
  /// Return true if the (vector) instruction I will be lowered to an
  /// instruction with a scalar splat operand for the given Operand number.
  bool canSplatOperand(Instruction *I, int Operand) const;
  /// Return true if a vector instruction will lower to a target instruction
  /// able to splat the given operand.
  bool canSplatOperand(unsigned Opcode, int Operand) const;

  bool isProfitableToSinkOperands(Instruction *I,
                                  SmallVectorImpl<Use *> &Ops) const;

  TTI::MemCmpExpansionOptions enableMemCmpExpansion(bool OptSize,
                                                    bool IsZeroCmp) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VCSIR_VCSIRTARGETTRANSFORMINFO_H
