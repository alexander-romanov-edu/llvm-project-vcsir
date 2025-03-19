#ifndef LLVM_LIB_TARGET_SIM_SIMTARGETMACHINE_H
#define LLVM_LIB_TARGET_SIM_SIMTARGETMACHINE_H

#include "SimSubtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <optional>

namespace llvm {
extern Target TheSimTarget;

class SimTargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  SimSubtarget Subtarget;

public:
  SimTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                   StringRef FS, const TargetOptions &Options,
                   std::optional<Reloc::Model> RM,
                   std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                   bool JIT);

  const SimSubtarget *getSubtargetImpl(const Function &) const override {
    SIM_DUMP_CYAN
    return &Subtarget;
  }
  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  TargetLoweringObjectFile *getObjFileLowering() const override;
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_SIM_SIMTARGETMACHINE_H
