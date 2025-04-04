//===-- VCSIRTargetMachine.cpp - Define TargetMachine for VCSIR ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about VCSIR target spec.
//
//===----------------------------------------------------------------------===//

#include "VCSIRTargetMachine.h"
#include "MCTargetDesc/VCSIRBaseInfo.h"
#include "TargetInfo/VCSIRTargetInfo.h"
#include "VCSIR.h"
#include "VCSIRMachineFunctionInfo.h"
#include "VCSIRTargetObjectFile.h"
#include "VCSIRTargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Vectorize/LoopIdiomVectorize.h"
#include <optional>
using namespace llvm;

static cl::opt<bool> EnableRedundantCopyElimination(
    "vcsir-enable-copyelim",
    cl::desc("Enable the redundant copy elimination pass"), cl::init(true),
    cl::Hidden);

// FIXME: Unify control over GlobalMerge.
static cl::opt<cl::boolOrDefault>
    EnableGlobalMerge("vcsir-enable-global-merge", cl::Hidden,
                      cl::desc("Enable the global merge pass"));

static cl::opt<bool>
    EnableMachineCombiner("vcsir-enable-machine-combiner",
                          cl::desc("Enable the machine combiner pass"),
                          cl::init(true), cl::Hidden);

static cl::opt<unsigned> RVVVectorBitsMaxOpt(
    "vcsir-v-vector-bits-max",
    cl::desc("Assume V extension vector registers are at most this big, "
             "with zero meaning no maximum size is assumed."),
    cl::init(0), cl::Hidden);

static cl::opt<int> RVVVectorBitsMinOpt(
    "vcsir-v-vector-bits-min",
    cl::desc("Assume V extension vector registers are at least this big, "
             "with zero meaning no minimum size is assumed. A value of -1 "
             "means use Zvl*b extension. This is primarily used to enable "
             "autovectorization with fixed width vectors."),
    cl::init(-1), cl::Hidden);

static cl::opt<bool> EnableVCSIRCopyPropagation(
    "vcsir-enable-copy-propagation",
    cl::desc("Enable the copy propagation with VCSIR copy instr"),
    cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableSinkFold("vcsir-enable-sink-fold",
                   cl::desc("Enable sinking and folding of instruction copies"),
                   cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableLoopDataPrefetch("vcsir-enable-loop-data-prefetch", cl::Hidden,
                           cl::desc("Enable the loop data prefetch pass"),
                           cl::init(true));

static cl::opt<bool> EnableMISchedLoadStoreClustering(
    "vcsir-misched-load-store-clustering", cl::Hidden,
    cl::desc("Enable load and store clustering in the machine scheduler"),
    cl::init(true));

static cl::opt<bool> EnablePostMISchedLoadStoreClustering(
    "vcsir-postmisched-load-store-clustering", cl::Hidden,
    cl::desc(
        "Enable PostRA load and store clustering in the machine scheduler"),
    cl::init(true));

static cl::opt<bool>
    EnableVLOptimizer("vcsir-enable-vl-optimizer",
                      cl::desc("Enable the VCSIR VL Optimizer pass"),
                      cl::init(true), cl::Hidden);

static cl::opt<bool> DisableVectorMaskMutation(
    "vcsir-disable-vector-mask-mutation",
    cl::desc("Disable the vector mask scheduling mutation"), cl::init(false),
    cl::Hidden);

static cl::opt<bool>
    EnableMachinePipeliner("vcsir-enable-pipeliner",
                           cl::desc("Enable Machine Pipeliner for VCSIR"),
                           cl::init(false), cl::Hidden);

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVCSIRTarget() {
  RegisterTargetMachine<VCSIRTargetMachine> X(getTheVCSIRTarget());
  auto *PR = PassRegistry::getPassRegistry();
  initializeGlobalISel(*PR);
  initializeVCSIRO0PreLegalizerCombinerPass(*PR);
  initializeVCSIRPostLegalizerCombinerPass(*PR);
  initializeKCFIPass(*PR);
  initializeVCSIRExpandPseudoPass(*PR);
  initializeVCSIRDAGToDAGISelLegacyPass(*PR);
}

static StringRef computeDataLayout(const Triple &TT,
                                   const TargetOptions &Options) {
  StringRef ABIName = Options.MCOptions.getABIName();
  if (TT.isArch64Bit()) {
    if (ABIName == "lp64e")
      return "e-m:e-p:64:64-i64:64-i128:128-n32:64-S64";

    return "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128";
  }
  assert(TT.isArch32Bit() && "only RV32 and RV64 are currently supported");

  if (ABIName == "ilp32e")
    return "e-m:e-p:32:32-i64:64-n32-S32";

  return "e-m:e-p:32:32-i64:64-n32-S128";
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

VCSIRTargetMachine::VCSIRTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, computeDataLayout(TT, Options), TT, CPU, FS,
                               Options, getEffectiveRelocModel(TT, RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<VCSIRELFTargetObjectFile>()) {
  initAsmInfo();

  // VCSIR supports the MachineOutliner.
  setMachineOutliner(true);
  setSupportsDefaultOutlining(true);

  if (TT.isOSFuchsia() && !TT.isArch64Bit())
    report_fatal_error("Fuchsia is only supported for 64-bit");

  setCFIFixup(true);
}

const VCSIRSubtarget *
VCSIRTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string TuneCPU =
      TuneAttr.isValid() ? TuneAttr.getValueAsString().str() : CPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  SmallString<512> Key;
  raw_svector_ostream(Key) << CPU << TuneCPU << FS;
  auto &I = SubtargetMap[Key];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    auto ABIName = Options.MCOptions.getABIName();
    if (const MDString *ModuleTargetABI = dyn_cast_or_null<MDString>(
            F.getParent()->getModuleFlag("target-abi"))) {
      ABIName = ModuleTargetABI->getString();
    }
    I = std::make_unique<VCSIRSubtarget>(TargetTriple, CPU, TuneCPU, FS, *this);
  }
  return I.get();
}

MachineFunctionInfo *VCSIRTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return VCSIRMachineFunctionInfo::create<VCSIRMachineFunctionInfo>(
      Allocator, F, static_cast<const VCSIRSubtarget *>(STI));
}

TargetTransformInfo
VCSIRTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(VCSIRTTIImpl(this, F));
}

// A VCSIR hart has a single byte-addressable address space of 2^XLEN bytes
// for all memory accesses, so it is reasonable to assume that an
// implementation has no-op address space casts. If an implementation makes a
// change to this, they can override it here.
bool VCSIRTargetMachine::isNoopAddrSpaceCast(unsigned SrcAS,
                                             unsigned DstAS) const {
  return true;
}

ScheduleDAGInstrs *
VCSIRTargetMachine::createMachineScheduler(MachineSchedContext *C) const {
  ScheduleDAGMILive *DAG = nullptr;
  if (EnableMISchedLoadStoreClustering) {
    DAG = createGenericSchedLive(C);
    DAG->addMutation(createLoadClusterDAGMutation(
        DAG->TII, DAG->TRI, /*ReorderWhileClustering=*/true));
    DAG->addMutation(createStoreClusterDAGMutation(
        DAG->TII, DAG->TRI, /*ReorderWhileClustering=*/true));
  }
  return DAG;
}

ScheduleDAGInstrs *
VCSIRTargetMachine::createPostMachineScheduler(MachineSchedContext *C) const {
  ScheduleDAGMI *DAG = nullptr;
  if (EnablePostMISchedLoadStoreClustering) {
    DAG = createGenericSchedPostRA(C);
    DAG->addMutation(createLoadClusterDAGMutation(
        DAG->TII, DAG->TRI, /*ReorderWhileClustering=*/true));
    DAG->addMutation(createStoreClusterDAGMutation(
        DAG->TII, DAG->TRI, /*ReorderWhileClustering=*/true));
  }

  return DAG;
}

namespace {

class VCSIRPassConfig : public TargetPassConfig {
public:
  VCSIRPassConfig(VCSIRTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    if (TM.getOptLevel() != CodeGenOptLevel::None)
      substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
    setEnableSinkAndFold(EnableSinkFold);
    EnableLoopTermFold = true;
  }

  VCSIRTargetMachine &getVCSIRTargetMachine() const {
    return getTM<VCSIRTargetMachine>();
  }

  void addIRPasses() override;
  bool addPreISel() override;
  void addCodeGenPrepare() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
  void addPreSched2() override;
  void addMachineSSAOptimization() override;
  bool addRegAssignAndRewriteFast() override;
  bool addRegAssignAndRewriteOptimized() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addFastRegAlloc() override;

  std::unique_ptr<CSEConfigBase> getCSEConfig() const override;
};
} // namespace

TargetPassConfig *VCSIRTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new VCSIRPassConfig(*this, PM);
}

std::unique_ptr<CSEConfigBase> VCSIRPassConfig::getCSEConfig() const {
  return getStandardCSEConfigForOpt(TM->getOptLevel());
}

bool VCSIRPassConfig::addRegAssignAndRewriteFast() {
  return TargetPassConfig::addRegAssignAndRewriteFast();
}

bool VCSIRPassConfig::addRegAssignAndRewriteOptimized() {
  addPass(createVirtRegRewriter(false));
  return TargetPassConfig::addRegAssignAndRewriteOptimized();
}

void VCSIRPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());

  if (getOptLevel() != CodeGenOptLevel::None) {
    if (EnableLoopDataPrefetch)
      addPass(createLoopDataPrefetchPass());

    addPass(createInterleavedAccessPass());
  }

  TargetPassConfig::addIRPasses();
}

bool VCSIRPassConfig::addPreISel() {
  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    // Add a barrier before instruction selection so that we will not get
    // deleted block address after enabling default outlining. See D99707 for
    // more details.
    addPass(createBarrierNoopPass());
  }

  if ((TM->getOptLevel() != CodeGenOptLevel::None &&
       EnableGlobalMerge == cl::BOU_UNSET) ||
      EnableGlobalMerge == cl::BOU_TRUE) {
    // FIXME: Like AArch64, we disable extern global merging by default due to
    // concerns it might regress some workloads. Unlike AArch64, we don't
    // currently support enabling the pass in an "OnlyOptimizeForSize" mode.
    // Investigating and addressing both items are TODO.
    addPass(createGlobalMergePass(TM, /* MaxOffset */ 2047,
                                  /* OnlyOptimizeForSize */ false,
                                  /* MergeExternalByDefault */ true));
  }

  return false;
}

void VCSIRPassConfig::addCodeGenPrepare() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createTypePromotionLegacyPass());
  TargetPassConfig::addCodeGenPrepare();
}

bool VCSIRPassConfig::addInstSelector() {
  addPass(createVCSIRISelDag(getVCSIRTargetMachine(), getOptLevel()));

  return false;
}

bool VCSIRPassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

void VCSIRPassConfig::addPreLegalizeMachineIR() {
  addPass(createVCSIRO0PreLegalizerCombiner());
}

bool VCSIRPassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

void VCSIRPassConfig::addPreRegBankSelect() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createVCSIRPostLegalizerCombiner());
}

bool VCSIRPassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool VCSIRPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  return false;
}

void VCSIRPassConfig::addPreSched2() {
  // Emit KCFI checks for indirect calls.
  addPass(createKCFIPass());
}

void VCSIRPassConfig::addPreEmitPass() {
  // TODO: It would potentially be better to schedule copy propagation after
  // expanding pseudos (in addPreEmitPass2). However, performing copy
  // propagation after the machine outliner (which runs after addPreEmitPass)
  // currently leads to incorrect code-gen, where copies to registers within
  // outlined functions are removed erroneously.
  if (TM->getOptLevel() >= CodeGenOptLevel::Default &&
      EnableVCSIRCopyPropagation)
    addPass(createMachineCopyPropagationPass(true));
  addPass(&BranchRelaxationPassID);
}

void VCSIRPassConfig::addPreEmitPass2() {
  //  addPass(createVCSIRIndirectBranchTrackingPass());
  addPass(createVCSIRExpandPseudoPass());

  // KCFI indirect call checks are lowered to a bundle.
  addPass(createUnpackMachineBundles([&](const MachineFunction &MF) {
    return MF.getFunction().getParent()->getModuleFlag("kcfi");
  }));
}

void VCSIRPassConfig::addMachineSSAOptimization() {
  TargetPassConfig::addMachineSSAOptimization();

  if (EnableMachineCombiner)
    addPass(&MachineCombinerID);
}

void VCSIRPassConfig::addPreRegAlloc() {
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableMachinePipeliner)
    addPass(&MachinePipelinerID);
}

void VCSIRPassConfig::addFastRegAlloc() {
  addPass(&InitUndefID);
  TargetPassConfig::addFastRegAlloc();
}

void VCSIRPassConfig::addPostRegAlloc() {}

void VCSIRTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerLateLoopOptimizationsEPCallback([=](LoopPassManager &LPM,
                                                 OptimizationLevel Level) {
    LPM.addPass(LoopIdiomVectorizePass(LoopIdiomVectorizeStyle::Predicated));
  });
}

yaml::MachineFunctionInfo *
VCSIRTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::VCSIRMachineFunctionInfo();
}

yaml::MachineFunctionInfo *
VCSIRTargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<VCSIRMachineFunctionInfo>();
  return new yaml::VCSIRMachineFunctionInfo(*MFI);
}

bool VCSIRTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const auto &YamlMFI =
      static_cast<const yaml::VCSIRMachineFunctionInfo &>(MFI);
  PFS.MF.getInfo<VCSIRMachineFunctionInfo>()->initializeBaseYamlFields(YamlMFI);
  return false;
}
