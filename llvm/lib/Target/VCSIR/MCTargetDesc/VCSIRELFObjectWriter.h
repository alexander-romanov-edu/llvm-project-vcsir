//===-- VCSIRELFObjectWriter.h ------------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_TARGET_VCSIR_MCTARGETDESC_VCSIRELFOBJWRITER_H
#define LLVM_LIB_TARGET_VCSIR_MCTARGETDESC_VCSIRELFOBJWRITER_H

#include <memory>

namespace llvm {
class MCObjectTargetWriter;
std::unique_ptr<MCObjectTargetWriter> createVCSIRELFObjectWriter(uint8_t OSABI);
} // namespace llvm
#endif
