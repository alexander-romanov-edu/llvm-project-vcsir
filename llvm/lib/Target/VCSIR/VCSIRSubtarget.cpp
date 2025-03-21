//===-- VCSIRSubtarget.cpp - Define Subtarget for the VCSIR -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VCSIRSubtarget.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;
#define DEBUG_TYPE "sim-subtarget"
#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "VCSIRGenSubtargetInfo.inc"
VCSIRSubtarget::VCSIRSubtarget(const StringRef &CPU, const StringRef &TuneCPU,
                               const StringRef &FS, const TargetMachine &TM)
    : VCSIRGenSubtargetInfo(TM.getTargetTriple(), CPU, TuneCPU, FS) {}
