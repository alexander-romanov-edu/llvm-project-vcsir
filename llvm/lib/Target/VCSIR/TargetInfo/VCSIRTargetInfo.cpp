#include "VCSIRTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheVCSIRTarget() {
  static Target TheVCSIRTarget;
  return TheVCSIRTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVCSIRTargetInfo() {
  RegisterTarget<Triple::vcsir> X(getTheVCSIRTarget(), "vcsir", "VCSIR 32",
                                   "VCSIR");
}
