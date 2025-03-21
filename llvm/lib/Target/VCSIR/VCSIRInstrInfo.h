//===-- VCSIRInstrInfo.h - VCSIR Instr Info -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRINSTRINFO_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRINSTRINFO_H

#include "MCTargetDesc/VCSIRBaseInfo.h"
#include "VCSIRRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "VCSIRGenInstrInfo.inc"

namespace llvm {

class VCSIRSubtarget;

class VCSIRInstrInfo : public VCSIRGenInstrInfo {
public:
  VCSIRInstrInfo();
};
} // end namespace llvm
#endif // LLVM_LIB_TARGET_VCSIR_VCSIRINSTRINFO_H
