#include "VCSIRTargetDesc.h"
#include "TargetInfo/VCSIRTargetInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_REGINFO_MC_DESC

#include "VCSIRGenRegisterInfo.inc"

static MCRegisterInfo *createSimMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitVCSIRMCRegisterInfo(X, VCSIR::X1);
  return X;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVCSIRTargetMC() {
  Target &TheSimTarget = getTheVCSIRTarget();
  TargetRegistry::RegisterMCRegInfo(TheSimTarget, createSimMCRegisterInfo);
}
