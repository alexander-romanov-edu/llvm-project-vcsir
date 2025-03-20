#include "VCSIRRegisterInfo.h"

#include "VCSIRFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

#define GET_REGINFO_ENUM
#define GET_REGINFO_TARGET_DESC
#include "VCSIRGenRegisterInfo.inc"

VCSIRRegisterInfo::VCSIRRegisterInfo() : VCSIRGenRegisterInfo(VCSIR::X1) {}
