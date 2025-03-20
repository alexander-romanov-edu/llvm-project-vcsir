#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRREGISTERINFO_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRREGISTERINFO_H

#define GET_REGINFO_HEADER
#include "VCSIRGenRegisterInfo.inc"

namespace llvm {

struct VCSIRRegisterInfo : public VCSIRGenRegisterInfo {
public:
  VCSIRRegisterInfo();
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VCSIR_VCSIRREGISTERINFO_H
