//===-- VCSIRMCObjectFileInfo.cpp - VCSIR object file properties ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the VCSIRMCObjectFileInfo properties.
//
//===----------------------------------------------------------------------===//

#include "VCSIRMCObjectFileInfo.h"
#include "VCSIRTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

unsigned
VCSIRMCObjectFileInfo::getTextSectionAlignment(const MCSubtargetInfo &STI) {
  return 4;
}

unsigned VCSIRMCObjectFileInfo::getTextSectionAlignment() const {
  return getTextSectionAlignment(*getContext().getSubtargetInfo());
}
