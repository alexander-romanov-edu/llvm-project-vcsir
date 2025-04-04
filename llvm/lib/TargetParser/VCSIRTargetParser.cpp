//===-- VCSIRTargetParser.cpp - Parser for target features ------*- C++ -*-===//
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

#include "llvm/TargetParser/VCSIRTargetParser.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
namespace llvm {
namespace VCSIR {

constexpr CPUInfo VCSIRCPUInfo[] = {
  {"generic-vcsir", "vcsirarch", false, false, {11, 11, 11}}
};

static const CPUInfo *getCPUInfoByName(StringRef CPU) {
  for (auto &C : VCSIRCPUInfo)
    if (C.Name == CPU)
      return &C;
  return nullptr;
}

bool hasFastScalarUnalignedAccess(StringRef CPU) {
  const CPUInfo *Info = getCPUInfoByName(CPU);
  return Info && Info->FastScalarUnalignedAccess;
}

bool hasFastVectorUnalignedAccess(StringRef CPU) {
  const CPUInfo *Info = getCPUInfoByName(CPU);
  return Info && Info->FastVectorUnalignedAccess;
}

bool hasValidCPUModel(StringRef CPU) {
  const CPUModel Model = getCPUModel(CPU);
  return Model.MVendorID != 0 && Model.MArchID != 0 && Model.MImpID != 0;
}

CPUModel getCPUModel(StringRef CPU) {
  const CPUInfo *Info = getCPUInfoByName(CPU);
  if (!Info)
    return {0, 0, 0};
  return Info->Model;
}

bool parseCPU(StringRef CPU, bool IsRV64 = false) {
  const CPUInfo *Info = getCPUInfoByName(CPU);

  if (!Info)
    return false;
  return Info->is64Bit() == IsRV64;
}

bool parseTuneCPU(StringRef TuneCPU, bool IsRV64) {
  // Fallback to parsing as a CPU.
  return parseCPU(TuneCPU, IsRV64);
}

StringRef getMArchFromMcpu(StringRef CPU) {
  const CPUInfo *Info = getCPUInfoByName(CPU);
  if (!Info)
    return "";
  return Info->DefaultMarch;
}

void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values, bool IsRV64) {
  for (const auto &C : VCSIRCPUInfo) {
    if (IsRV64 == C.is64Bit())
      Values.emplace_back(C.Name);
  }
}

void fillValidTuneCPUArchList(SmallVectorImpl<StringRef> &Values, bool IsRV64) {
  for (const auto &C : VCSIRCPUInfo) {
    if (IsRV64 == C.is64Bit())
      Values.emplace_back(C.Name);
  }
}

// This function is currently used by IREE, so it's not dead code.
void getFeaturesForCPU(StringRef CPU,
                       SmallVectorImpl<std::string> &EnabledFeatures,
                       bool NeedPlus) {
}

} // namespace VCSIR
} // namespace llvm
