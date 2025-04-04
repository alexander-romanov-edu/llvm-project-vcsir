//=== VCSIRO0PreLegalizerCombiner.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "VCSIR.h"
#include "VCSIRSubtarget.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"

#define GET_GICOMBINER_DEPS
#include "VCSIRGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "vcsir-O0-prelegalizer-combiner"

using namespace llvm;

namespace {
#define GET_GICOMBINER_TYPES
#include "VCSIRGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

class VCSIRO0PreLegalizerCombinerImpl : public Combiner {
protected:
  const CombinerHelper Helper;
  const VCSIRO0PreLegalizerCombinerImplRuleConfig &RuleConfig;
  const VCSIRSubtarget &STI;

public:
  VCSIRO0PreLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
      const VCSIRO0PreLegalizerCombinerImplRuleConfig &RuleConfig,
      const VCSIRSubtarget &STI);

  static const char *getName() { return "VCSIRO0PreLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "VCSIRGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "VCSIRGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

VCSIRO0PreLegalizerCombinerImpl::VCSIRO0PreLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
    const VCSIRO0PreLegalizerCombinerImplRuleConfig &RuleConfig,
    const VCSIRSubtarget &STI)
    : Combiner(MF, CInfo, TPC, &KB, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ true, &KB), RuleConfig(RuleConfig),
      STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "VCSIRGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

// Pass boilerplate
// ================

class VCSIRO0PreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  VCSIRO0PreLegalizerCombiner();

  StringRef getPassName() const override {
    return "VCSIRO0PreLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  VCSIRO0PreLegalizerCombinerImplRuleConfig RuleConfig;
};
} // end anonymous namespace

void VCSIRO0PreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

VCSIRO0PreLegalizerCombiner::VCSIRO0PreLegalizerCombiner()
    : MachineFunctionPass(ID) {
  initializeVCSIRO0PreLegalizerCombinerPass(*PassRegistry::getPassRegistry());

  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

bool VCSIRO0PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  auto &TPC = getAnalysis<TargetPassConfig>();

  const Function &F = MF.getFunction();
  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);

  const VCSIRSubtarget &ST = MF.getSubtarget<VCSIRSubtarget>();

  CombinerInfo CInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, /*EnableOpt*/ false,
                     F.hasOptSize(), F.hasMinSize());
  // Disable fixed-point iteration in the Combiner. This improves compile-time
  // at the cost of possibly missing optimizations. See PR#94291 for details.
  CInfo.MaxIterations = 1;

  VCSIRO0PreLegalizerCombinerImpl Impl(MF, CInfo, &TPC, *KB,
                                       /*CSEInfo*/ nullptr, RuleConfig, ST);
  return Impl.combineMachineInstrs();
}

char VCSIRO0PreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(VCSIRO0PreLegalizerCombiner, DEBUG_TYPE,
                      "Combine VCSIR machine instrs before legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_END(VCSIRO0PreLegalizerCombiner, DEBUG_TYPE,
                    "Combine VCSIR machine instrs before legalization", false,
                    false)

namespace llvm {
FunctionPass *createVCSIRO0PreLegalizerCombiner() {
  return new VCSIRO0PreLegalizerCombiner();
}
} // end namespace llvm
