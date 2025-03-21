//===-- VCSIRSubtarget.h - Define Subtarget for the VCSIR -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRSUBTARGET_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRSUBTARGET_H

#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_SUBTARGETINFO_HEADER
#include "VCSIRGenSubtargetInfo.inc"

namespace llvm {

class VCSIRSubtarget : public VCSIRGenSubtargetInfo {
public:
  VCSIRSubtarget(const StringRef &CPU, const StringRef &TuneCPU,
                 const StringRef &FS, const TargetMachine &TM);

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VCSIR_VCSIRSUBTARGET_H
