//===- VCSIRMatInt.cpp - Immediate materialisation -------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VCSIRMatInt.h"
#include "VCSIRTargetDesc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {
static int getInstSeqCost(VCSIRMatInt::InstSeq &Res, bool HasRVC) {
  if (!HasRVC)
    return Res.size();

  int Cost = 0;
  for (auto Instr : Res) {
    // Assume instructions that aren't listed aren't compressible.
    bool Compressed = false;
    switch (Instr.getOpcode()) {
    case VCSIR::SLLI:
    case VCSIR::SRLI:
      Compressed = true;
      break;
    case VCSIR::ADDI:
    case VCSIR::LUI:
      Compressed = isInt<6>(Instr.getImm());
      break;
    }
    // Two RVC instructions take the same space as one RVI instruction, but
    // can take longer to execute than the single RVI instruction. Thus, we
    // consider that two RVC instruction are slightly more costly than one
    // RVI instruction. For longer sequences of RVC instructions the space
    // savings can be worth it, though. The costs below try to model that.
    if (!Compressed)
      Cost += 100; // Baseline cost of one RVI instruction: 100%.
    else
      Cost += 70; // 70% cost of baseline.
  }
  return Cost;
}

// Recursively generate a sequence for materializing an integer.
static void generateInstSeqImpl(int64_t Val, const MCSubtargetInfo &STI,
                                VCSIRMatInt::InstSeq &Res) {
  if (isInt<32>(Val)) {
    // Depending on the active bits in the immediate Value v, the following
    // instruction sequences are emitted:
    //
    // v == 0                        : ADDI
    // v[0,12) != 0 && v[12,32) == 0 : ADDI
    // v[0,12) == 0 && v[12,32) != 0 : LUI
    // v[0,32) != 0                  : LUI+ADDI(W)
    int64_t Hi20 = ((Val + 0x800) >> 12) & 0xFFFFF;
    int64_t Lo12 = SignExtend64<12>(Val);

    if (Hi20)
      Res.emplace_back(VCSIR::LUI, Hi20);

    if (Lo12 || Hi20 == 0) {
      unsigned AddiOpc = VCSIR::ADDI;
      Res.emplace_back(AddiOpc, Lo12);
    }
    return;
  }

  llvm_unreachable("Can't emit >32-bit imm for VCSIR");
}

static unsigned extractRotateInfo(int64_t Val) {
  // for case: 0b111..1..xxxxxx1..1..
  unsigned LeadingOnes = llvm::countl_one((uint64_t)Val);
  unsigned TrailingOnes = llvm::countr_one((uint64_t)Val);
  if (TrailingOnes > 0 && TrailingOnes < 64 &&
      (LeadingOnes + TrailingOnes) > (64 - 12))
    return 64 - TrailingOnes;

  // for case: 0bxxx1..1..1...xxx
  unsigned UpperTrailingOnes = llvm::countr_one(Hi_32(Val));
  unsigned LowerLeadingOnes = llvm::countl_one(Lo_32(Val));
  if (UpperTrailingOnes < 32 &&
      (UpperTrailingOnes + LowerLeadingOnes) > (64 - 12))
    return 32 - UpperTrailingOnes;

  return 0;
}

static void generateInstSeqLeadingZeros(int64_t Val, const MCSubtargetInfo &STI,
                                        VCSIRMatInt::InstSeq &Res) {
  assert(Val > 0 && "Expected positive val");

  unsigned LeadingZeros = llvm::countl_zero((uint64_t)Val);
  uint64_t ShiftedVal = (uint64_t)Val << LeadingZeros;
  // Fill in the bits that will be shifted out with 1s. An example where this
  // helps is trailing one masks with 32 or more ones. This will generate
  // ADDI -1 and an SRLI.
  ShiftedVal |= maskTrailingOnes<uint64_t>(LeadingZeros);

  VCSIRMatInt::InstSeq TmpSeq;
  generateInstSeqImpl(ShiftedVal, STI, TmpSeq);

  // Keep the new sequence if it is an improvement or the original is empty.
  if ((TmpSeq.size() + 1) < Res.size() || (Res.empty() && TmpSeq.size() < 8)) {
    TmpSeq.emplace_back(VCSIR::SRLI, LeadingZeros);
    Res = TmpSeq;
  }

  // Some cases can benefit from filling the lower bits with zeros instead.
  ShiftedVal &= maskTrailingZeros<uint64_t>(LeadingZeros);
  TmpSeq.clear();
  generateInstSeqImpl(ShiftedVal, STI, TmpSeq);

  // Keep the new sequence if it is an improvement or the original is empty.
  if ((TmpSeq.size() + 1) < Res.size() || (Res.empty() && TmpSeq.size() < 8)) {
    TmpSeq.emplace_back(VCSIR::SRLI, LeadingZeros);
    Res = TmpSeq;
  }
}

namespace VCSIRMatInt {
InstSeq generateInstSeq(int64_t Val, const MCSubtargetInfo &STI) {
  VCSIRMatInt::InstSeq Res;
  generateInstSeqImpl(Val, STI, Res);

  // If the low 12 bits are non-zero, the first expansion may end with an ADDI
  // or ADDIW. If there are trailing zeros, try generating a sign extended
  // constant with no trailing zeros and use a final SLLI to restore them.
  if ((Val & 0xfff) != 0 && (Val & 1) == 0 && Res.size() >= 2) {
    unsigned TrailingZeros = llvm::countr_zero((uint64_t)Val);
    int64_t ShiftedVal = Val >> TrailingZeros;
    // If we can use C.LI+C.SLLI instead of LUI+ADDI(W) prefer that since
    // its more compressible. But only if LUI+ADDI(W) isn't fusable.
    // NOTE: We don't check for C extension to minimize differences in generated
    // code.
    bool IsShiftedCompressible = isInt<6>(ShiftedVal);
    VCSIRMatInt::InstSeq TmpSeq;
    generateInstSeqImpl(ShiftedVal, STI, TmpSeq);

    // Keep the new sequence if it is an improvement.
    if ((TmpSeq.size() + 1) < Res.size() || IsShiftedCompressible) {
      TmpSeq.emplace_back(VCSIR::SLLI, TrailingZeros);
      Res = TmpSeq;
    }
  }

  // If we have a 1 or 2 instruction sequence this is the best we can do. This
  // will always be true for RV32 and will often be true for RV64.
  if (Res.size() <= 2)
    return Res;

  llvm_unreachable("Expected RV32 to only need 2 instructions");
}

void generateMCInstSeq(int64_t Val, const MCSubtargetInfo &STI,
                       MCRegister DestReg, SmallVectorImpl<MCInst> &Insts) {
  VCSIRMatInt::InstSeq Seq = VCSIRMatInt::generateInstSeq(Val, STI);

  MCRegister SrcReg = VCSIR::X0;
  for (VCSIRMatInt::Inst &Inst : Seq) {
    switch (Inst.getOpndKind()) {
    case VCSIRMatInt::Imm:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addImm(Inst.getImm()));
      break;
    case VCSIRMatInt::RegX0:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addReg(SrcReg)
                          .addReg(VCSIR::X0));
      break;
    case VCSIRMatInt::RegReg:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addReg(SrcReg)
                          .addReg(SrcReg));
      break;
    case VCSIRMatInt::RegImm:
      Insts.push_back(MCInstBuilder(Inst.getOpcode())
                          .addReg(DestReg)
                          .addReg(SrcReg)
                          .addImm(Inst.getImm()));
      break;
    }

    // Only the first instruction has X0 as its source.
    SrcReg = DestReg;
  }
}

InstSeq generateTwoRegInstSeq(int64_t Val, const MCSubtargetInfo &STI,
                              unsigned &ShiftAmt, unsigned &AddOpc) {
  int64_t LoVal = SignExtend64<32>(Val);
  if (LoVal == 0)
    return VCSIRMatInt::InstSeq();

  // Subtract the LoVal to emulate the effect of the final ADD.
  uint64_t Tmp = (uint64_t)Val - (uint64_t)LoVal;
  assert(Tmp != 0);

  // Use trailing zero counts to figure how far we need to shift LoVal to line
  // up with the remaining constant.
  // TODO: This algorithm assumes all non-zero bits in the low 32 bits of the
  // final constant come from LoVal.
  unsigned TzLo = llvm::countr_zero((uint64_t)LoVal);
  unsigned TzHi = llvm::countr_zero(Tmp);
  assert(TzLo < 32 && TzHi >= 32);
  ShiftAmt = TzHi - TzLo;
  AddOpc = VCSIR::ADD;

  if (Tmp == ((uint64_t)LoVal << ShiftAmt))
    return VCSIRMatInt::generateInstSeq(LoVal, STI);
  return VCSIRMatInt::InstSeq();
}

int getIntMatCost(const APInt &Val, unsigned Size, const MCSubtargetInfo &STI,
                  bool CompressionCost, bool FreeZeroes) {
  int PlatRegSize = 32;

  // Split the constant into platform register sized chunks, and calculate cost
  // of each chunk.
  int Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < Size; ShiftVal += PlatRegSize) {
    APInt Chunk = Val.ashr(ShiftVal).sextOrTrunc(PlatRegSize);
    if (FreeZeroes && Chunk.getSExtValue() == 0)
      continue;
    InstSeq MatSeq = generateInstSeq(Chunk.getSExtValue(), STI);
    Cost += getInstSeqCost(MatSeq, false);
  }
  return std::max(FreeZeroes ? 0 : 1, Cost);
}

OpndKind Inst::getOpndKind() const {
  switch (Opc) {
  default:
    llvm_unreachable("Unexpected opcode!");
  case VCSIR::LUI:
    return VCSIRMatInt::Imm;
  case VCSIR::ADDI:
  case VCSIR::XORI:
  case VCSIR::SLLI:
  case VCSIR::SRLI:
    return VCSIRMatInt::RegImm;
  }
}

} // namespace VCSIRMatInt
} // namespace llvm
