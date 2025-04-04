//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VCSIRSelectionDAGInfo.h"
#include "VCSIRISelLowering.h"

using namespace llvm;

VCSIRSelectionDAGInfo::~VCSIRSelectionDAGInfo() = default;

bool VCSIRSelectionDAGInfo::isTargetMemoryOpcode(unsigned Opcode) const {
  return false;
}

bool VCSIRSelectionDAGInfo::isTargetStrictFPOpcode(unsigned Opcode) const {
  return false;
}
