//===-- VCSIR.h - Top-level interface for VCSIR ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// VCSIR back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_VCSIR_H
#define LLVM_LIB_TARGET_VCSIR_VCSIR_H

#include "MCTargetDesc/VCSIRBaseInfo.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class FunctionPass;
class InstructionSelector;
class PassRegistry;
class VCSIRRegisterBankInfo;
class VCSIRSubtarget;
class VCSIRTargetMachine;

FunctionPass *createVCSIRCodeGenPreparePass();
void initializeVCSIRCodeGenPreparePass(PassRegistry &);

// FunctionPass *createVCSIRIndirectBranchTrackingPass();
// void initializeVCSIRIndirectBranchTrackingPass(PassRegistry &);

FunctionPass *createVCSIRISelDag(VCSIRTargetMachine &TM,
                                 CodeGenOptLevel OptLevel);

FunctionPass *createVCSIRExpandPseudoPass();
void initializeVCSIRExpandPseudoPass(PassRegistry &);

FunctionPass *createVCSIRExpandAtomicPseudoPass();
void initializeVCSIRExpandAtomicPseudoPass(PassRegistry &);

FunctionPass *createVCSIRPreRAExpandPseudoPass();
void initializeVCSIRPreRAExpandPseudoPass(PassRegistry &);

FunctionPass *createVCSIRLoadStoreOptPass();
void initializeVCSIRLoadStoreOptPass(PassRegistry &);

FunctionPass *createVCSIRZacasABIFixPass();
void initializeVCSIRZacasABIFixPass(PassRegistry &);

InstructionSelector *
createVCSIRInstructionSelector(const VCSIRTargetMachine &,
                               const VCSIRSubtarget &,
                               const VCSIRRegisterBankInfo &);
void initializeVCSIRDAGToDAGISelLegacyPass(PassRegistry &);

FunctionPass *createVCSIRPostLegalizerCombiner();
void initializeVCSIRPostLegalizerCombinerPass(PassRegistry &);

FunctionPass *createVCSIRO0PreLegalizerCombiner();
void initializeVCSIRO0PreLegalizerCombinerPass(PassRegistry &);

} // namespace llvm

#endif
