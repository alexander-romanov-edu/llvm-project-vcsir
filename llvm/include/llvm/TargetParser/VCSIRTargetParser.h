//===-- VCSIRTargetParser - Parser for target features ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise hardware features
// for VCSIR CPUs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_VCSIRTARGETPARSER_H
#define LLVM_TARGETPARSER_VCSIRTARGETPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Triple;

namespace VCSIR {

namespace VCSIRExtensionBitmaskTable {
struct VCSIRExtensionBitmask {
  const char *Name;
  unsigned GroupID;
  unsigned BitPosition;
};
} // namespace VCSIRExtensionBitmaskTable

struct CPUModel {
  uint32_t MVendorID;
  uint64_t MArchID;
  uint64_t MImpID;
};

struct CPUInfo {
  StringLiteral Name;
  StringLiteral DefaultMarch;
  bool FastScalarUnalignedAccess;
  bool FastVectorUnalignedAccess;
  CPUModel Model;
  bool is64Bit() const { return DefaultMarch.starts_with("rv64"); }
};

// We use 64 bits as the known part in the scalable vector types.
static constexpr unsigned RVVBitsPerBlock = 64;

void getFeaturesForCPU(StringRef CPU,
                       SmallVectorImpl<std::string> &EnabledFeatures,
                       bool NeedPlus = false);
bool parseCPU(StringRef CPU, bool IsRV64);
bool parseTuneCPU(StringRef CPU, bool IsRV64);
StringRef getMArchFromMcpu(StringRef CPU);
void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values, bool IsRV64);
void fillValidTuneCPUArchList(SmallVectorImpl<StringRef> &Values, bool IsRV64);
bool hasFastScalarUnalignedAccess(StringRef CPU);
bool hasFastVectorUnalignedAccess(StringRef CPU);
bool hasValidCPUModel(StringRef CPU);
CPUModel getCPUModel(StringRef CPU);

} // namespace VCSIR
} // namespace llvm

#endif
