//===-- VCSIRInstrInfo.cpp - VCSIR Instruction Information -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the VCSIR implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "VCSIRInstrInfo.h"
#include "MCTargetDesc/VCSIRBaseInfo.h"
#include "MCTargetDesc/VCSIRMatInt.h"
#include "VCSIR.h"
#include "VCSIRMachineFunctionInfo.h"
#include "VCSIRSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineCombinerPattern.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#define GET_INSTRINFO_NAMED_OPS
#include "VCSIRGenInstrInfo.inc"

#define DEBUG_TYPE "VCSIR-inst-info"

namespace llvm {
unsigned VCSIRInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  return 32;
}
void VCSIRInstrInfo::movImm(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            const DebugLoc &DL, Register DstReg, uint64_t Val,
                            MachineInstr::MIFlag Flag, bool DstRenamable,
                            bool DstIsDead) const {
  Register SrcReg = VCSIR::X0;

  // For RV32, allow a sign or unsigned 32 bit value.
  if (!isInt<32>(Val)) {
    // If have a uimm32 it will still fit in a register so we can allow it.
    if (!isUInt<32>(Val))
      report_fatal_error("Should only materialize 32-bit constants");

    // Sign extend for generateInstSeq.
    Val = SignExtend64<32>(Val);
  }

  VCSIRMatInt::InstSeq Seq = VCSIRMatInt::generateInstSeq(Val, STI);
  assert(!Seq.empty());

  bool SrcRenamable = false;
  unsigned Num = 0;

  for (const VCSIRMatInt::Inst &Inst : Seq) {
    bool LastItem = ++Num == Seq.size();
    unsigned DstRegState = getDeadRegState(DstIsDead && LastItem) |
                           getRenamableRegState(DstRenamable);
    unsigned SrcRegState = getKillRegState(SrcReg != VCSIR::X0) |
                           getRenamableRegState(SrcRenamable);
    switch (Inst.getOpndKind()) {
    case VCSIRMatInt::Imm:
      BuildMI(MBB, MBBI, DL, get(Inst.getOpcode()))
          .addReg(DstReg, RegState::Define | DstRegState)
          .addImm(Inst.getImm())
          .setMIFlag(Flag);
      break;
    case VCSIRMatInt::RegX0:
      BuildMI(MBB, MBBI, DL, get(Inst.getOpcode()))
          .addReg(DstReg, RegState::Define | DstRegState)
          .addReg(SrcReg, SrcRegState)
          .addReg(VCSIR::X0)
          .setMIFlag(Flag);
      break;
    case VCSIRMatInt::RegReg:
      BuildMI(MBB, MBBI, DL, get(Inst.getOpcode()))
          .addReg(DstReg, RegState::Define | DstRegState)
          .addReg(SrcReg, SrcRegState)
          .addReg(SrcReg, SrcRegState)
          .setMIFlag(Flag);
      break;
    case VCSIRMatInt::RegImm:
      BuildMI(MBB, MBBI, DL, get(Inst.getOpcode()))
          .addReg(DstReg, RegState::Define | DstRegState)
          .addReg(SrcReg, SrcRegState)
          .addImm(Inst.getImm())
          .setMIFlag(Flag);
      break;
    }

    // Only the first instruction has X0 as its source.
    SrcReg = DstReg;
    SrcRenamable = DstRenamable;
  }
}

unsigned VCSIRCC::getBrCond(VCSIRCC::CondCode CC, bool Imm) {
  switch (CC) {
  default:
    llvm_unreachable("Unknown condition code!");
  case VCSIRCC::COND_EQ:
    return VCSIR::BEQ;
  case VCSIRCC::COND_NE:
    return VCSIR::BNE;
  case VCSIRCC::COND_LT:
    return VCSIR::BLT;
  case VCSIRCC::COND_GE:
    return VCSIR::BGE;
  case VCSIRCC::COND_LTU:
    return VCSIR::BLTU;
  case VCSIRCC::COND_GEU:
    return VCSIR::BGEU;
  }
}

const MCInstrDesc &VCSIRInstrInfo::getBrCond(VCSIRCC::CondCode CC,
                                             bool Imm) const {
  return get(VCSIRCC::getBrCond(CC, Imm));
}

VCSIRCC::CondCode VCSIRCC::getOppositeBranchCondition(VCSIRCC::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Unrecognized conditional branch");
  case VCSIRCC::COND_EQ:
    return VCSIRCC::COND_NE;
  case VCSIRCC::COND_NE:
    return VCSIRCC::COND_EQ;
  case VCSIRCC::COND_LT:
    return VCSIRCC::COND_GE;
  case VCSIRCC::COND_GE:
    return VCSIRCC::COND_LT;
  case VCSIRCC::COND_LTU:
    return VCSIRCC::COND_GEU;
  case VCSIRCC::COND_GEU:
    return VCSIRCC::COND_LTU;
  }
}

bool VCSIRInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert((Cond.size() == 3) && "Invalid branch condition!");
  auto CC = static_cast<VCSIRCC::CondCode>(Cond[0].getImm());
  Cond[0].setImm(getOppositeBranchCondition(CC));
  return false;
}
} // namespace llvm
