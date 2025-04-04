//===-- VCSIRInstrInfo.h - VCSIR Instr Info -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRINSTRINFO_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRINSTRINFO_H

#include "VCSIR.h"
#include "VCSIRRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/MC/MCInstrDesc.h"

#define GET_INSTRINFO_HEADER
#define GET_INSTRINFO_OPERAND_ENUM
#include "VCSIRGenInstrInfo.inc"
#include "VCSIRGenRegisterInfo.inc"

namespace llvm {

class VCSIRSubtarget;
namespace VCSIRCC {

enum CondCode {
  COND_EQ,
  COND_NE,
  COND_LT,
  COND_GE,
  COND_LTU,
  COND_GEU,
  COND_INVALID
};

CondCode getOppositeBranchCondition(CondCode);
unsigned getBrCond(CondCode CC, bool Imm = false);

} // end of namespace VCSIRCC
class VCSIRInstrInfo : public VCSIRGenInstrInfo {
protected:
  const VCSIRSubtarget &STI;

public:
  VCSIRInstrInfo(VCSIRSubtarget &ST) : VCSIRGenInstrInfo(), STI(ST) {}
  // Materializes the given integer Val into DstReg.
  void movImm(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
              const DebugLoc &DL, Register DstReg, uint64_t Val,
              MachineInstr::MIFlag Flag = MachineInstr::NoFlags,
              bool DstRenamable = false, bool DstIsDead = false) const;

  const MCInstrDesc &getBrCond(VCSIRCC::CondCode CC, bool Imm = false) const;

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;
};
} // end namespace llvm
#endif // LLVM_LIB_TARGET_VCSIR_VCSIRINSTRINFO_H
