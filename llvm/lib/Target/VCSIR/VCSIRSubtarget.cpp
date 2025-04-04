//===-- VCSIRSubtarget.cpp - Define Subtarget for the VCSIR -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VCSIRSubtarget.h"
#include "GISel/VCSIRCallLowering.h"
#include "GISel/VCSIRLegalizerInfo.h"
#include "VCSIRSelectionDAGInfo.h"
#include "VCSIRTargetMachine.h"

#include "llvm/Target/TargetMachine.h"
using namespace llvm;
#define DEBUG_TYPE "vcsir-subtarget"
#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "VCSIRGenSubtargetInfo.inc"
VCSIRSubtarget::VCSIRSubtarget(const Triple &TT, const StringRef &CPU,
                               const StringRef &TuneCPU, const StringRef &FS,
                               const TargetMachine &TM)
    : VCSIRGenSubtargetInfo(TT, CPU, TuneCPU, FS), FrameLowering(*this),
      InstrInfo(*this), RegInfo(), TLInfo(TM, *this),
      TSInfo() {}

const SelectionDAGTargetInfo *VCSIRSubtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}

const CallLowering *VCSIRSubtarget::getCallLowering() const {
  if (!CallLoweringInfo)
    CallLoweringInfo.reset(new VCSIRCallLowering(*getTargetLowering()));
  return CallLoweringInfo.get();
}

InstructionSelector *VCSIRSubtarget::getInstructionSelector() const {
  if (!InstSelector) {
    InstSelector.reset(createVCSIRInstructionSelector(
        *static_cast<const VCSIRTargetMachine *>(&TLInfo.getTargetMachine()),
        *this, *getRegBankInfo()));
  }
  return InstSelector.get();
}

const LegalizerInfo *VCSIRSubtarget::getLegalizerInfo() const {
  if (!Legalizer)
    Legalizer.reset(new VCSIRLegalizerInfo(*this));
  return Legalizer.get();
}

const VCSIRRegisterBankInfo *VCSIRSubtarget::getRegBankInfo() const {
  if (!RegBankInfo)
    RegBankInfo.reset(new VCSIRRegisterBankInfo(getHwMode()));
  return RegBankInfo.get();
}
