//===-- VCSIRSubtarget.h - Define Subtarget for the VCSIR -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRSUBTARGET_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRSUBTARGET_H

#include "GISel/VCSIRRegisterBankInfo.h"
#include "MCTargetDesc/VCSIRBaseInfo.h"
#include "VCSIRFrameLowering.h"
#include "VCSIRISelLowering.h"
#include "VCSIRInstrInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"
#include <bitset>

#define GET_SUBTARGETINFO_HEADER
#include "VCSIRGenSubtargetInfo.inc"

namespace llvm {

class VCSIRSubtarget : public VCSIRGenSubtargetInfo {
  VCSIRFrameLowering FrameLowering;
  VCSIRInstrInfo InstrInfo;
  VCSIRRegisterInfo RegInfo;
  VCSIRTargetLowering TLInfo;
#define GET_SUBTARGETINFO_MACRO(ATTRIBUTE, DEFAULT, GETTER)                    \
  bool ATTRIBUTE = DEFAULT;
#include "VCSIRGenSubtargetInfo.inc"

  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;
  mutable std::unique_ptr<CallLowering> CallLoweringInfo;
  mutable std::unique_ptr<InstructionSelector> InstSelector;
  mutable std::unique_ptr<LegalizerInfo> Legalizer;
  mutable std::unique_ptr<VCSIRRegisterBankInfo> RegBankInfo;

public:
  VCSIRSubtarget(const Triple &TT, const StringRef &CPU,
                 const StringRef &TuneCPU, const StringRef &FS,
                 const TargetMachine &TM);

  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override;
  const CallLowering *getCallLowering() const override;
  InstructionSelector *getInstructionSelector() const override;
  const LegalizerInfo *getLegalizerInfo() const override;
  const VCSIRRegisterBankInfo *getRegBankInfo() const override;

  const TargetFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
  const VCSIRInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const VCSIRRegisterInfo *getRegisterInfo() const override { return &RegInfo; }
  unsigned getXLen() const { return 32; }

  MVT getXLenVT() const { return MVT::i32; }
  const VCSIRTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VCSIR_VCSIRSUBTARGET_H
