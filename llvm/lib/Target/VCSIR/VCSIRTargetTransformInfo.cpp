//===-- VCSIRTargetTransformInfo.cpp - VCSIR specific TTI ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VCSIRTargetTransformInfo.h"
#include "MCTargetDesc/VCSIRMatInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/ErrorHandling.h"
#include <cmath>
#include <optional>
using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "vcsirtti"

static cl::opt<unsigned> RVVRegisterWidthLMUL(
    "vcsir-v-register-bit-width-lmul",
    cl::desc(
        "The LMUL to use for getRegisterBitWidth queries. Affects LMUL used "
        "by autovectorized code. Fractional LMULs are not supported."),
    cl::init(2), cl::Hidden);

static cl::opt<unsigned> SLPMaxVF(
    "vcsir-v-slp-max-vf",
    cl::desc("Overrides result used for getMaximumVF query which is used "
             "exclusively by SLP vectorizer."),
    cl::Hidden);

InstructionCost
VCSIRTTIImpl::getVCSIRInstructionCost(ArrayRef<unsigned> OpCodes, MVT VT,
                                      TTI::TargetCostKind CostKind) {
  return InstructionCost::getInvalid();
}

static InstructionCost getIntImmCostImpl(const DataLayout &DL,
                                         const VCSIRSubtarget *ST,
                                         const APInt &Imm, Type *Ty,
                                         TTI::TargetCostKind CostKind,
                                         bool FreeZeroes) {
  assert(Ty->isIntegerTy() &&
         "getIntImmCost can only estimate cost of materialising integers");

  // We have a Zero register, so 0 is always free.
  if (Imm == 0)
    return TTI::TCC_Free;

  // Otherwise, we check how many instructions it will take to materialise.
  return VCSIRMatInt::getIntMatCost(Imm, DL.getTypeSizeInBits(Ty), *ST,
                                    /*CompressionCost=*/false, FreeZeroes);
}

InstructionCost VCSIRTTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                                            TTI::TargetCostKind CostKind) {
  return getIntImmCostImpl(getDataLayout(), getST(), Imm, Ty, CostKind, false);
}

// Look for patterns of shift followed by AND that can be turned into a pair of
// shifts. We won't need to materialize an immediate for the AND so these can
// be considered free.
static bool canUseShiftPair(Instruction *Inst, const APInt &Imm) {
  uint64_t Mask = Imm.getZExtValue();
  auto *BO = dyn_cast<BinaryOperator>(Inst->getOperand(0));
  if (!BO || !BO->hasOneUse())
    return false;

  if (BO->getOpcode() != Instruction::Shl)
    return false;

  if (!isa<ConstantInt>(BO->getOperand(1)))
    return false;

  unsigned ShAmt = cast<ConstantInt>(BO->getOperand(1))->getZExtValue();
  // (and (shl x, c2), c1) will be matched to (srli (slli x, c2+c3), c3) if c1
  // is a mask shifted by c2 bits with c3 leading zeros.
  if (isShiftedMask_64(Mask)) {
    unsigned Trailing = llvm::countr_zero(Mask);
    if (ShAmt == Trailing)
      return true;
  }

  return false;
}

InstructionCost VCSIRTTIImpl::getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                                const APInt &Imm, Type *Ty,
                                                TTI::TargetCostKind CostKind,
                                                Instruction *Inst) {
  assert(Ty->isIntegerTy() &&
         "getIntImmCost can only estimate cost of materialising integers");

  // We have a Zero register, so 0 is always free.
  if (Imm == 0)
    return TTI::TCC_Free;

  // Some instructions in VCSIR can take a 12-bit immediate. Some of these are
  // commutative, in others the immediate comes from a specific argument index.
  bool Takes12BitImm = false;
  unsigned ImmArgIdx = ~0U;

  switch (Opcode) {
  case Instruction::GetElementPtr:
    // Never hoist any arguments to a GetElementPtr. CodeGenPrepare will
    // split up large offsets in GEP into better parts than ConstantHoisting
    // can.
    return TTI::TCC_Free;
  case Instruction::Store: {
    // Use the materialization cost regardless of if it's the address or the
    // value that is constant, except for if the store is misaligned and
    // misaligned accesses are not legal (experience shows constant hoisting
    // can sometimes be harmful in such cases).
    if (Idx == 1 || !Inst)
      return getIntImmCostImpl(getDataLayout(), getST(), Imm, Ty, CostKind,
                               /*FreeZeroes=*/true);

    StoreInst *ST = cast<StoreInst>(Inst);
    if (!getTLI()->allowsMemoryAccessForAlignment(
            Ty->getContext(), DL, getTLI()->getValueType(DL, Ty),
            ST->getPointerAddressSpace(), ST->getAlign()))
      return TTI::TCC_Free;

    return getIntImmCostImpl(getDataLayout(), getST(), Imm, Ty, CostKind,
                             /*FreeZeroes=*/true);
  }
  case Instruction::Load:
    // If the address is a constant, use the materialization cost.
    return getIntImmCost(Imm, Ty, CostKind);
  case Instruction::And:
    // zext.w
    if (Imm == UINT64_C(0xffffffff))
      return TTI::TCC_Free;
    if (Inst && Idx == 1 && Imm.getBitWidth() <= 32 &&
        canUseShiftPair(Inst, Imm))
      return TTI::TCC_Free;
    Takes12BitImm = true;
    break;
  case Instruction::Add:
    Takes12BitImm = true;
    break;
  case Instruction::Or:
  case Instruction::Xor:
    Takes12BitImm = true;
    break;
  case Instruction::Mul:
    // Power of 2 is a shift. Negated power of 2 is a shift and a negate.
    if (Imm.isPowerOf2() || Imm.isNegatedPowerOf2())
      return TTI::TCC_Free;
    // One more or less than a power of 2 can use SLLI+ADD/SUB.
    if ((Imm + 1).isPowerOf2() || (Imm - 1).isPowerOf2())
      return TTI::TCC_Free;
    // FIXME: There is no MULI instruction.
    Takes12BitImm = true;
    break;
  case Instruction::Sub:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    Takes12BitImm = true;
    ImmArgIdx = 1;
    break;
  default:
    break;
  }

  if (Takes12BitImm) {
    // Check immediate is the correct argument...
    if (Instruction::isCommutative(Opcode) || Idx == ImmArgIdx) {
      // ... and fits into the 12-bit immediate.
      if (Imm.getSignificantBits() <= 64 &&
          getTLI()->isLegalAddImmediate(Imm.getSExtValue())) {
        return TTI::TCC_Free;
      }
    }

    // Otherwise, use the full materialisation cost.
    return getIntImmCost(Imm, Ty, CostKind);
  }

  // By default, prevent hoisting.
  return TTI::TCC_Free;
}

InstructionCost
VCSIRTTIImpl::getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                  const APInt &Imm, Type *Ty,
                                  TTI::TargetCostKind CostKind) {
  // Prevent hoisting in unknown cases.
  return TTI::TCC_Free;
}

bool VCSIRTTIImpl::shouldExpandReduction(const IntrinsicInst *II) const {
  // Currently, the ExpandReductions pass can't expand scalable-vector
  // reductions, but we still request expansion as RVV doesn't support certain
  // reductions and the SelectionDAG can't legalize them either.
  switch (II->getIntrinsicID()) {
  default:
    return false;
  // These reductions have no equivalent in RVV
  case Intrinsic::vector_reduce_mul:
  case Intrinsic::vector_reduce_fmul:
    return true;
  }
}

std::optional<unsigned> VCSIRTTIImpl::getMaxVScale() const {
  return BaseT::getMaxVScale();
}

std::optional<unsigned> VCSIRTTIImpl::getVScaleForTuning() const {
  return BaseT::getVScaleForTuning();
}

TypeSize
VCSIRTTIImpl::getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
  unsigned LMUL =
      llvm::bit_floor(std::clamp<unsigned>(RVVRegisterWidthLMUL, 1, 8));
  if (K == TargetTransformInfo::RGK_Scalar)
    return TypeSize::getFixed(32);

  llvm_unreachable("Unsupported register kind");
}

InstructionCost
VCSIRTTIImpl::getConstantPoolLoadCost(Type *Ty, TTI::TargetCostKind CostKind) {
  // Add a cost of address generation + the cost of the load. The address
  // is expected to be a PC relative offset to a constant pool entry
  // using auipc/addi.
  return 2 + getMemoryOpCost(Instruction::Load, Ty, DL.getABITypeAlign(Ty),
                             /*AddressSpace=*/0, CostKind);
}

static bool isRepeatedConcatMask(ArrayRef<int> Mask, int &SubVectorSize) {
  unsigned Size = Mask.size();
  if (!isPowerOf2_32(Size))
    return false;
  for (unsigned I = 0; I != Size; ++I) {
    if (static_cast<unsigned>(Mask[I]) == I)
      continue;
    if (Mask[I] != 0)
      return false;
    if (Size % I != 0)
      return false;
    for (unsigned J = I + 1; J != Size; ++J)
      // Check the pattern is repeated.
      if (static_cast<unsigned>(Mask[J]) != J % I)
        return false;
    SubVectorSize = I;
    return true;
  }
  // That means Mask is <0, 1, 2, 3>. This is not a concatenation.
  return false;
}

static VectorType *getVRGatherIndexType(MVT DataVT, const VCSIRSubtarget &ST,
                                        LLVMContext &C) {
  assert((DataVT.getScalarSizeInBits() != 8 ||
          DataVT.getVectorNumElements() <= 256) &&
         "unhandled case in lowering");
  MVT IndexVT = DataVT.changeTypeToInteger();
  if (IndexVT.getScalarType().bitsGT(ST.getXLenVT()))
    IndexVT = IndexVT.changeVectorElementType(MVT::i16);
  return cast<VectorType>(EVT(IndexVT).getTypeForEVT(C));
}

/// Try to perform better estimation of the permutation.
/// 1. Split the source/destination vectors into real registers.
/// 2. Do the mask analysis to identify which real registers are
/// permuted. If more than 1 source registers are used for the
/// destination register building, the cost for this destination register
/// is (Number_of_source_register - 1) * Cost_PermuteTwoSrc. If only one
/// source register is used, build mask and calculate the cost as a cost
/// of PermuteSingleSrc.
/// Also, for the single register permute we try to identify if the
/// destination register is just a copy of the source register or the
/// copy of the previous destination register (the cost is
/// TTI::TCC_Basic). If the source register is just reused, the cost for
/// this operation is 0.
static InstructionCost
costShuffleViaVRegSplitting(VCSIRTTIImpl &TTI, MVT LegalVT,
                            std::optional<unsigned> VLen, VectorType *Tp,
                            ArrayRef<int> Mask, TTI::TargetCostKind CostKind) {
  assert(LegalVT.isFixedLengthVector());
  InstructionCost NumOfDests = InstructionCost::getInvalid();
  if (VLen && !Mask.empty()) {
    MVT ElemVT = LegalVT.getVectorElementType();
    unsigned ElemsPerVReg = *VLen / ElemVT.getFixedSizeInBits();
    LegalVT = TTI.getTypeLegalizationCost(
                     FixedVectorType::get(Tp->getElementType(), ElemsPerVReg))
                  .second;
    // Number of destination vectors after legalization:
    NumOfDests = divideCeil(Mask.size(), LegalVT.getVectorNumElements());
  }
  if (!NumOfDests.isValid() || NumOfDests <= 1 ||
      LegalVT.getVectorElementType().getSizeInBits() !=
          Tp->getElementType()->getPrimitiveSizeInBits() ||
      LegalVT.getVectorNumElements() >= Tp->getElementCount().getFixedValue())
    return InstructionCost::getInvalid();

  unsigned VecTySize = TTI.getDataLayout().getTypeStoreSize(Tp);
  unsigned LegalVTSize = LegalVT.getStoreSize();
  // Number of source vectors after legalization:
  unsigned NumOfSrcs = divideCeil(VecTySize, LegalVTSize);

  auto *SingleOpTy = FixedVectorType::get(Tp->getElementType(),
                                          LegalVT.getVectorNumElements());

  unsigned E = *NumOfDests.getValue();
  unsigned NormalizedVF =
      LegalVT.getVectorNumElements() * std::max(NumOfSrcs, E);
  unsigned NumOfSrcRegs = NormalizedVF / LegalVT.getVectorNumElements();
  unsigned NumOfDestRegs = NormalizedVF / LegalVT.getVectorNumElements();
  SmallVector<int> NormalizedMask(NormalizedVF, PoisonMaskElem);
  assert(NormalizedVF >= Mask.size() &&
         "Normalized mask expected to be not shorter than original mask.");
  copy(Mask, NormalizedMask.begin());
  InstructionCost Cost = 0;
  SmallBitVector ExtractedRegs(2 * NumOfSrcRegs);
  int NumShuffles = 0;
  processShuffleMasks(
      NormalizedMask, NumOfSrcRegs, NumOfDestRegs, NumOfDestRegs, []() {},
      [&](ArrayRef<int> RegMask, unsigned SrcReg, unsigned DestReg) {
        if (ExtractedRegs.test(SrcReg)) {
          Cost += TTI.getShuffleCost(TTI::SK_ExtractSubvector, Tp, {}, CostKind,
                                     (SrcReg % NumOfSrcRegs) *
                                         SingleOpTy->getNumElements(),
                                     SingleOpTy);
          ExtractedRegs.set(SrcReg);
        }
        if (!ShuffleVectorInst::isIdentityMask(RegMask, RegMask.size())) {
          ++NumShuffles;
          Cost += TTI.getShuffleCost(TTI::SK_PermuteSingleSrc, SingleOpTy,
                                     RegMask, CostKind, 0, nullptr);
          return;
        }
      },
      [&](ArrayRef<int> RegMask, unsigned Idx1, unsigned Idx2, bool NewReg) {
        if (ExtractedRegs.test(Idx1)) {
          Cost += TTI.getShuffleCost(
              TTI::SK_ExtractSubvector, Tp, {}, CostKind,
              (Idx1 % NumOfSrcRegs) * SingleOpTy->getNumElements(), SingleOpTy);
          ExtractedRegs.set(Idx1);
        }
        if (ExtractedRegs.test(Idx2)) {
          Cost += TTI.getShuffleCost(
              TTI::SK_ExtractSubvector, Tp, {}, CostKind,
              (Idx2 % NumOfSrcRegs) * SingleOpTy->getNumElements(), SingleOpTy);
          ExtractedRegs.set(Idx2);
        }
        Cost += TTI.getShuffleCost(TTI::SK_PermuteTwoSrc, SingleOpTy, RegMask,
                                   CostKind, 0, nullptr);
        NumShuffles += 2;
      });
  // Note: check that we do not emit too many shuffles here to prevent code
  // size explosion.
  // TODO: investigate, if it can be improved by extra analysis of the masks
  // to check if the code is more profitable.
  if ((NumOfDestRegs > 2 && NumShuffles <= static_cast<int>(NumOfDestRegs)) ||
      (NumOfDestRegs <= 2 && NumShuffles < 4))
    return Cost;
  return InstructionCost::getInvalid();
}

InstructionCost
VCSIRTTIImpl::getMaskedMemoryOpCost(unsigned Opcode, Type *Src, Align Alignment,
                                    unsigned AddressSpace,
                                    TTI::TargetCostKind CostKind) {
  if (!isLegalMaskedLoadStore(Src, Alignment) ||
      CostKind != TTI::TCK_RecipThroughput)
    return BaseT::getMaskedMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                        CostKind);

  return getMemoryOpCost(Opcode, Src, Alignment, AddressSpace, CostKind);
}

InstructionCost VCSIRTTIImpl::getInterleavedMemoryOpCost(
    unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
    Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
    bool UseMaskForCond, bool UseMaskForGaps) {
  // TODO: Return the cost of interleaved accesses for scalable vector when
  // unable to convert to segment accesses instructions.
  if (isa<ScalableVectorType>(VecTy))
    return InstructionCost::getInvalid();

  auto *FVTy = cast<FixedVectorType>(VecTy);
  InstructionCost MemCost =
      getMemoryOpCost(Opcode, VecTy, Alignment, AddressSpace, CostKind);
  unsigned VF = FVTy->getNumElements() / Factor;

  // An interleaved load will look like this for Factor=3:
  // %wide.vec = load <12 x i32>, ptr %3, align 4
  // %strided.vec = shufflevector %wide.vec, poison, <4 x i32> <stride mask>
  // %strided.vec1 = shufflevector %wide.vec, poison, <4 x i32> <stride mask>
  // %strided.vec2 = shufflevector %wide.vec, poison, <4 x i32> <stride mask>
  if (Opcode == Instruction::Load) {
    InstructionCost Cost = MemCost;
    for (unsigned Index : Indices) {
      FixedVectorType *VecTy =
          FixedVectorType::get(FVTy->getElementType(), VF * Factor);
      auto Mask = createStrideMask(Index, Factor, VF);
      Mask.resize(VF * Factor, -1);
      InstructionCost ShuffleCost =
          getShuffleCost(TTI::ShuffleKind::SK_PermuteSingleSrc, VecTy, Mask,
                         CostKind, 0, nullptr, {});
      Cost += ShuffleCost;
    }
    return Cost;
  }

  // TODO: Model for NF > 2
  // We'll need to enhance getShuffleCost to model shuffles that are just
  // inserts and extracts into subvectors, since they won't have the full cost
  // of a vrgather.
  // An interleaved store for 3 vectors of 4 lanes will look like
  // %11 = shufflevector <4 x i32> %4, <4 x i32> %6, <8 x i32> <0...7>
  // %12 = shufflevector <4 x i32> %9, <4 x i32> poison, <8 x i32> <0...3>
  // %13 = shufflevector <8 x i32> %11, <8 x i32> %12, <12 x i32> <0...11>
  // %interleaved.vec = shufflevector %13, poison, <12 x i32> <interleave mask>
  // store <12 x i32> %interleaved.vec, ptr %10, align 4
  if (Factor != 2)
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace, CostKind,
                                             UseMaskForCond, UseMaskForGaps);

  assert(Opcode == Instruction::Store && "Opcode must be a store");
  // For an interleaving store of 2 vectors, we perform one large interleaving
  // shuffle that goes into the wide store
  auto Mask = createInterleaveMask(VF, Factor);
  InstructionCost ShuffleCost =
      getShuffleCost(TTI::ShuffleKind::SK_PermuteSingleSrc, FVTy, Mask,
                     CostKind, 0, nullptr, {});
  return MemCost + ShuffleCost;
}

InstructionCost VCSIRTTIImpl::getGatherScatterOpCost(
    unsigned Opcode, Type *DataTy, const Value *Ptr, bool VariableMask,
    Align Alignment, TTI::TargetCostKind CostKind, const Instruction *I) {
  if (CostKind != TTI::TCK_RecipThroughput)
    return BaseT::getGatherScatterOpCost(Opcode, DataTy, Ptr, VariableMask,
                                         Alignment, CostKind, I);

  if ((Opcode == Instruction::Load &&
       !isLegalMaskedGather(DataTy, Align(Alignment))) ||
      (Opcode == Instruction::Store &&
       !isLegalMaskedScatter(DataTy, Align(Alignment))))
    return BaseT::getGatherScatterOpCost(Opcode, DataTy, Ptr, VariableMask,
                                         Alignment, CostKind, I);
  llvm_unreachable("Unsupported vector op");
}

InstructionCost VCSIRTTIImpl::getStridedMemoryOpCost(
    unsigned Opcode, Type *DataTy, const Value *Ptr, bool VariableMask,
    Align Alignment, TTI::TargetCostKind CostKind, const Instruction *I) {
  if (((Opcode == Instruction::Load || Opcode == Instruction::Store) &&
       !isLegalStridedLoadStore(DataTy, Alignment)) ||
      (Opcode != Instruction::Load && Opcode != Instruction::Store))
    return BaseT::getStridedMemoryOpCost(Opcode, DataTy, Ptr, VariableMask,
                                         Alignment, CostKind, I);

  if (CostKind == TTI::TCK_CodeSize)
    return TTI::TCC_Basic;

  llvm_unreachable("Unsupported vector");
}

InstructionCost
VCSIRTTIImpl::getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys) {
  InstructionCost Cost = 0;
  return Cost;
}

InstructionCost
VCSIRTTIImpl::getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                    TTI::TargetCostKind CostKind) {
  auto *RetTy = ICA.getReturnType();
  switch (ICA.getID()) {
  case Intrinsic::lrint:
  case Intrinsic::llrint:
    // We can't currently lower half or bfloat vector lrint/llrint.
    if (auto *VecTy = dyn_cast<VectorType>(ICA.getArgTypes()[0]);
        VecTy && VecTy->getElementType()->is16bitFPTy())
      return InstructionCost::getInvalid();
    [[fallthrough]];
  case Intrinsic::ceil:
  case Intrinsic::floor:
  case Intrinsic::trunc:
  case Intrinsic::rint:
  case Intrinsic::round:
  case Intrinsic::roundeven: {
    // These all use the same code.
    auto LT = getTypeLegalizationCost(RetTy);
    if (!LT.second.isVector() && TLI->isOperationCustom(ISD::FCEIL, LT.second))
      return LT.first * 8;
    break;
  }
  case Intrinsic::umin:
  case Intrinsic::umax:
  case Intrinsic::smin:
  case Intrinsic::smax:
    break;
  case Intrinsic::sadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::usub_sat:
    break;
  case Intrinsic::fma:
  case Intrinsic::fmuladd:
  case Intrinsic::fabs:
    llvm_unreachable("No fpu allowed");
  case Intrinsic::sqrt:
  case Intrinsic::cttz:
  case Intrinsic::ctlz:
  case Intrinsic::ctpop:
  case Intrinsic::abs:
  case Intrinsic::get_active_lane_mask:
    break;
  }
  return BaseT::getIntrinsicInstrCost(ICA, CostKind);
}

InstructionCost VCSIRTTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst,
                                               Type *Src,
                                               TTI::CastContextHint CCH,
                                               TTI::TargetCostKind CostKind,
                                               const Instruction *I) {
  bool IsVectorType = isa<VectorType>(Dst) && isa<VectorType>(Src);
  if (!IsVectorType)
    return BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I);
  llvm_unreachable("No vector types allowed");
}

InstructionCost
VCSIRTTIImpl::getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                     FastMathFlags FMF,
                                     TTI::TargetCostKind CostKind) {
  return BaseT::getMinMaxReductionCost(IID, Ty, FMF, CostKind);
}

InstructionCost
VCSIRTTIImpl::getArithmeticReductionCost(unsigned Opcode, VectorType *Ty,
                                         std::optional<FastMathFlags> FMF,
                                         TTI::TargetCostKind CostKind) {
  return BaseT::getArithmeticReductionCost(Opcode, Ty, FMF, CostKind);
}

InstructionCost VCSIRTTIImpl::getExtendedReductionCost(
    unsigned Opcode, bool IsUnsigned, Type *ResTy, VectorType *ValTy,
    FastMathFlags FMF, TTI::TargetCostKind CostKind) {
  return BaseT::getExtendedReductionCost(Opcode, IsUnsigned, ResTy, ValTy, FMF,
                                         CostKind);
}
InstructionCost VCSIRTTIImpl::getStoreImmCost(Type *Ty,
                                              TTI::OperandValueInfo OpInfo,
                                              TTI::TargetCostKind CostKind) {
  assert(OpInfo.isConstant() && "non constant operand?");
  return 0;
}

InstructionCost VCSIRTTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src,
                                              MaybeAlign Alignment,
                                              unsigned AddressSpace,
                                              TTI::TargetCostKind CostKind,
                                              TTI::OperandValueInfo OpInfo,
                                              const Instruction *I) {
  EVT VT = TLI->getValueType(DL, Src, true);
  // Type legalization can't handle structs
  if (VT == MVT::Other)
    return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                  CostKind, OpInfo, I);

  InstructionCost Cost = 0;
  if (Opcode == Instruction::Store && OpInfo.isConstant())
    Cost += getStoreImmCost(Src, OpInfo, CostKind);

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Src);

  InstructionCost BaseCost = [&]() {
    InstructionCost Cost = LT.first;
    if (CostKind != TTI::TCK_RecipThroughput)
      return Cost;

    return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                  CostKind, OpInfo, I);
  }();

  return Cost + BaseCost;
}

InstructionCost VCSIRTTIImpl::getCmpSelInstrCost(
    unsigned Opcode, Type *ValTy, Type *CondTy, CmpInst::Predicate VecPred,
    TTI::TargetCostKind CostKind, TTI::OperandValueInfo Op1Info,
    TTI::OperandValueInfo Op2Info, const Instruction *I) {
  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind,
                                   Op1Info, Op2Info, I);
}

InstructionCost VCSIRTTIImpl::getCFInstrCost(unsigned Opcode,
                                             TTI::TargetCostKind CostKind,
                                             const Instruction *I) {
  if (CostKind != TTI::TCK_RecipThroughput)
    return Opcode == Instruction::PHI ? 0 : 1;
  // Branches are assumed to be predicted.
  return 0;
}

InstructionCost VCSIRTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args, const Instruction *CxtI) {

  return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                       Args, CxtI);
}

// TODO: Deduplicate from TargetTransformInfoImplCRTPBase.
InstructionCost VCSIRTTIImpl::getPointersChainCost(
    ArrayRef<const Value *> Ptrs, const Value *Base,
    const TTI::PointersChainInfo &Info, Type *AccessTy,
    TTI::TargetCostKind CostKind) {
  InstructionCost Cost = TTI::TCC_Free;
  // In the basic model we take into account GEP instructions only
  // (although here can come alloca instruction, a value, constants and/or
  // constant expressions, PHIs, bitcasts ... whatever allowed to be used as a
  // pointer). Typically, if Base is a not a GEP-instruction and all the
  // pointers are relative to the same base address, all the rest are
  // either GEP instructions, PHIs, bitcasts or constants. When we have same
  // base, we just calculate cost of each non-Base GEP as an ADD operation if
  // any their index is a non-const.
  // If no known dependencies between the pointers cost is calculated as a sum
  // of costs of GEP instructions.
  for (auto [I, V] : enumerate(Ptrs)) {
    const auto *GEP = dyn_cast<GetElementPtrInst>(V);
    if (!GEP)
      continue;
    if (Info.isSameBase() && V != Base) {
      if (GEP->hasAllConstantIndices())
        continue;
      // If the chain is unit-stride and BaseReg + stride*i is a legal
      // addressing mode, then presume the base GEP is sitting around in a
      // register somewhere and check if we can fold the offset relative to
      // it.
      unsigned Stride = DL.getTypeStoreSize(AccessTy);
      if (Info.isUnitStride() &&
          isLegalAddressingMode(AccessTy,
                                /* BaseGV */ nullptr,
                                /* BaseOffset */ Stride * I,
                                /* HasBaseReg */ true,
                                /* Scale */ 0,
                                GEP->getType()->getPointerAddressSpace()))
        continue;
      Cost += getArithmeticInstrCost(Instruction::Add, GEP->getType(), CostKind,
                                     {TTI::OK_AnyValue, TTI::OP_None},
                                     {TTI::OK_AnyValue, TTI::OP_None}, {});
    } else {
      SmallVector<const Value *> Indices(GEP->indices());
      Cost += getGEPCost(GEP->getSourceElementType(), GEP->getPointerOperand(),
                         Indices, AccessTy, CostKind);
    }
  }
  return Cost;
}

void VCSIRTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                           TTI::UnrollingPreferences &UP,
                                           OptimizationRemarkEmitter *ORE) {
  return BasicTTIImplBase::getUnrollingPreferences(L, SE, UP, ORE);
}

void VCSIRTTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                         TTI::PeelingPreferences &PP) {
  BaseT::getPeelingPreferences(L, SE, PP);
}

unsigned VCSIRTTIImpl::getRegUsageForType(Type *Ty) const {
  return BaseT::getRegUsageForType(Ty);
}

TTI::AddressingModeKind
VCSIRTTIImpl::getPreferredAddressingMode(const Loop *L,
                                         ScalarEvolution *SE) const {
  return BasicTTIImplBase::getPreferredAddressingMode(L, SE);
}

bool VCSIRTTIImpl::isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                                 const TargetTransformInfo::LSRCost &C2) {
  // VCSIR specific here are "instruction number 1st priority".
  // If we need to emit adds inside the loop to add up base registers, then
  // we need at least one extra temporary register.
  unsigned C1NumRegs = C1.NumRegs + (C1.NumBaseAdds != 0);
  unsigned C2NumRegs = C2.NumRegs + (C2.NumBaseAdds != 0);
  return std::tie(C1.Insns, C1NumRegs, C1.AddRecCost, C1.NumIVMuls,
                  C1.NumBaseAdds, C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
         std::tie(C2.Insns, C2NumRegs, C2.AddRecCost, C2.NumIVMuls,
                  C2.NumBaseAdds, C2.ScaleCost, C2.ImmCost, C2.SetupCost);
}

bool VCSIRTTIImpl::isLegalMaskedExpandLoad(Type *DataTy, Align Alignment) {
  return false;
}

bool VCSIRTTIImpl::isLegalMaskedCompressStore(Type *DataTy, Align Alignment) {
  return false;
}

/// See if \p I should be considered for address type promotion. We check if \p
/// I is a sext with right type and used in memory accesses. If it used in a
/// "complex" getelementptr, we allow it to be promoted without finding other
/// sext instructions that sign extended the same initial value. A getelementptr
/// is considered as "complex" if it has more than 2 operands.
bool VCSIRTTIImpl::shouldConsiderAddressTypePromotion(
    const Instruction &I, bool &AllowPromotionWithoutCommonHeader) {
  bool Considerable = false;
  AllowPromotionWithoutCommonHeader = false;
  if (!isa<SExtInst>(&I))
    return false;
  Type *ConsideredSExtType =
      Type::getInt64Ty(I.getParent()->getParent()->getContext());
  if (I.getType() != ConsideredSExtType)
    return false;
  // See if the sext is the one with the right type and used in at least one
  // GetElementPtrInst.
  for (const User *U : I.users()) {
    if (const GetElementPtrInst *GEPInst = dyn_cast<GetElementPtrInst>(U)) {
      Considerable = true;
      // A getelementptr is considered as "complex" if it has more than 2
      // operands. We will promote a SExt used in such complex GEP as we
      // expect some computation to be merged if they are done on 64 bits.
      if (GEPInst->getNumOperands() > 2) {
        AllowPromotionWithoutCommonHeader = true;
        break;
      }
    }
  }
  return Considerable;
}

bool VCSIRTTIImpl::canSplatOperand(unsigned Opcode, int Operand) const {
  switch (Opcode) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::ICmp:
  case Instruction::FCmp:
    return true;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::Select:
    return Operand == 1;
  default:
    return false;
  }
}

bool VCSIRTTIImpl::canSplatOperand(Instruction *I, int Operand) const {
  return false;
}

bool VCSIRTTIImpl::isProfitableToSinkOperands(
    Instruction *I, SmallVectorImpl<Use *> &Ops) const {
  return false;
}

VCSIRTTIImpl::TTI::MemCmpExpansionOptions
VCSIRTTIImpl::enableMemCmpExpansion(bool OptSize, bool IsZeroCmp) const {
  TTI::MemCmpExpansionOptions Options;
  return Options;
}
