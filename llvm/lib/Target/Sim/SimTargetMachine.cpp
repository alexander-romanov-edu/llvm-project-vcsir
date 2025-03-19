#include "SimTargetMachine.h"
#include "Sim.h"
#include "TargetInfo/SimTargetInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSimTarget() {
  // Register the target.
  SIM_DUMP_CYAN
  RegisterTargetMachine<SimTargetMachine> A(getTheSimTarget());
}

SimTargetMachine::SimTargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &Options,
                                   std::optional<Reloc::Model> RM,
                                   std::optional<CodeModel::Model> CM,
                                   CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(
          T, "e-m:e-p:32:32-i8:8:32-i16:16:32-i64:64-n32", TT, CPU, FS, Options,
          Reloc::Static, getEffectiveCodeModel(CM, CodeModel::Small), OL) {
  SIM_DUMP_CYAN
  initAsmInfo();
}

namespace {

/// Sim Code Generator Pass Configuration Options.
class SimPassConfig : public TargetPassConfig {
public:
  SimPassConfig(SimTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  SimTargetMachine &getSimTargetMachine() const {
    return getTM<SimTargetMachine>();
  }

  bool addInstSelector() override {
    SIM_DUMP_CYAN
    addPass(createSimISelDag(getSimTargetMachine(), getOptLevel()));
    return false;
  }
};

} // end anonymous namespace

TargetPassConfig *SimTargetMachine::createPassConfig(PassManagerBase &PM) {
  SIM_DUMP_CYAN
  return new SimPassConfig(*this, PM);
}
