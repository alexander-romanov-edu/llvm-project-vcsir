//===-- VCSIRMCTargetDesc.cpp - VCSIR Target Descriptions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VCSIRTargetDesc.h"
#include "MCTargetDesc/VCSIRBaseInfo.h"

#include "TargetInfo/VCSIRTargetInfo.h"
#include "VCSIRMCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_REGINFO_MC_DESC
#include "VCSIRGenRegisterInfo.inc"
#undef GET_REGINFO_MC_DESC

#define GET_INSTRINFO_MC_DESC
#include "VCSIRGenInstrInfo.inc"
#undef GET_INSTRINFO_ENUM

#define GET_SUBTARGETINFO_MC_DESC
#include "VCSIRGenSubtargetInfo.inc"

static MCRegisterInfo *createVCSIRMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitVCSIRMCRegisterInfo(X, VCSIR::X1);
  return X;
}

static MCInstrInfo *createVCSIRMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitVCSIRMCInstrInfo(X);
  return X;
}

static MCSubtargetInfo *
createVCSIRMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createVCSIRMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCAsmInfo *createVCSIRMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new VCSIRELFMCAsmInfo(TT);
  unsigned SP = MRI.getDwarfRegNum(VCSIR::X1, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVCSIRTargetMC() {
  Target &TheVCSIRTarget = getTheVCSIRTarget();
  RegisterMCAsmInfoFn X(TheVCSIRTarget, createVCSIRMCAsmInfo);
  TargetRegistry::RegisterMCRegInfo(TheVCSIRTarget, createVCSIRMCRegisterInfo);
  TargetRegistry::RegisterMCInstrInfo(TheVCSIRTarget, createVCSIRMCInstrInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheVCSIRTarget,
                                          createVCSIRMCSubtargetInfo);
}
