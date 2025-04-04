//===--- VCSIR.cpp - Implement VCSIR target feature support --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements VCSIR TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "VCSIR.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/VCSIRTargetParser.h"
#include <optional>

using namespace clang;
using namespace clang::targets;

ArrayRef<const char *> VCSIRTargetInfo::getGCCRegNames() const {
  // clang-format off
  static const char *const GCCRegNames[] = {
      // Integer registers
      "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
      "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
      "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
      "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31",
    };
  // clang-format on
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> VCSIRTargetInfo::getGCCRegAliases() const {
  static const TargetInfo::GCCRegAlias GCCRegAliases[] = {
      {{"zero"}, "x0"}, {{"ra"}, "x1"},   {{"sp"}, "x2"},    {{"gp"}, "x3"},
      {{"tp"}, "x4"},   {{"t0"}, "x5"},   {{"t1"}, "x6"},    {{"t2"}, "x7"},
      {{"s0"}, "x8"},   {{"s1"}, "x9"},   {{"a0"}, "x10"},   {{"a1"}, "x11"},
      {{"a2"}, "x12"},  {{"a3"}, "x13"},  {{"a4"}, "x14"},   {{"a5"}, "x15"},
      {{"a6"}, "x16"},  {{"a7"}, "x17"},  {{"s2"}, "x18"},   {{"s3"}, "x19"},
      {{"s4"}, "x20"},  {{"s5"}, "x21"},  {{"s6"}, "x22"},   {{"s7"}, "x23"},
      {{"s8"}, "x24"},  {{"s9"}, "x25"},  {{"s10"}, "x26"},  {{"s11"}, "x27"},
      {{"t3"}, "x28"},  {{"t4"}, "x29"},  {{"t5"}, "x30"},   {{"t6"}, "x31"}};
  return llvm::ArrayRef(GCCRegAliases);
}

bool VCSIRTargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  case 'I':
    // A 12-bit signed immediate.
    Info.setRequiresImmediate(-2048, 2047);
    return true;
  case 'J':
    // Integer zero.
    Info.setRequiresImmediate(0);
    return true;
  case 'K':
    // A 5-bit unsigned immediate for CSR access instructions.
    Info.setRequiresImmediate(0, 31);
    return true;
  case 'A':
    // An address that is held in a general-purpose register.
    Info.setAllowsMemory();
    return true;
  case 's':
  case 'S': // A symbol or label reference with a constant offset
    Info.setAllowsRegister();
    return true;
  case 'R':
    // An even-odd GPR pair
    Info.setAllowsRegister();
    return true;
  }
}

std::string VCSIRTargetInfo::convertConstraint(const char *&Constraint) const {
  return TargetInfo::convertConstraint(Constraint);
}

static unsigned getVersionValue(unsigned MajorVersion, unsigned MinorVersion) {
  return MajorVersion * 1000000 + MinorVersion * 1000;
}

void VCSIRTargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("__vcsir");
  Builder.defineMacro("__vcsir_xlen", "32");
  StringRef CodeModel = getTargetOpts().CodeModel;
  if (CodeModel == "default")
    CodeModel = "small";

  if (CodeModel == "small")
    Builder.defineMacro("__vcsir_cmodel_medlow");
  else if (CodeModel == "medium")
    Builder.defineMacro("__vcsir_cmodel_medany");
  else if (CodeModel == "large")
    Builder.defineMacro("__vcsir_cmodel_large");

  StringRef ABIName = getABI();

  if (ABIName == "ilp32e" || ABIName == "lp64e")
    Builder.defineMacro("__vcsir_abi_rve");

  Builder.defineMacro("__vcsir_arch_test");

  Builder.defineMacro("__vcsir_div");
  Builder.defineMacro("__vcsir_muldiv");
  
  if (FastScalarUnalignedAccess)
    Builder.defineMacro("__vcsir_misaligned_fast");
  else
    Builder.defineMacro("__vcsir_misaligned_avoid");
}

static constexpr int NumVCSIRBuiltins = 0;
static constexpr int NumBuiltins = 0;

#define GET_BUILTIN_STR_TABLE
#include "clang/Basic/BuiltinsVCSIR.inc"
#undef GET_BUILTIN_STR_TABLE

static constexpr Builtin::Info BuiltinInfos[1] = {
#define GET_BUILTIN_INFOS
#include "clang/Basic/BuiltinsVCSIR.inc"
#undef GET_BUILTIN_INFOS
};

llvm::SmallVector<Builtin::InfosShard>
VCSIRTargetInfo::getTargetBuiltins() const {
  return {};
}

bool VCSIRTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  Features["32bit"] = true;
  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

std::optional<std::pair<unsigned, unsigned>>
VCSIRTargetInfo::getVScaleRange(const LangOptions &LangOpts,
                                bool IsArmStreamingFunction) const {
  return std::nullopt;
}

/// Return true if has this feature, need to sync with handleTargetFeatures.
bool VCSIRTargetInfo::hasFeature(StringRef Feature) const {
  return false;
}

/// Perform initialization based on the user configured set of features.
bool VCSIRTargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                           DiagnosticsEngine &Diags) {
  return true;
}

bool VCSIRTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::VCSIR::parseCPU(Name, false);
}

void VCSIRTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  bool Is64Bit = false;
  llvm::VCSIR::fillValidCPUArchList(Values, Is64Bit);
}

bool VCSIRTargetInfo::isValidTuneCPUName(StringRef Name) const {
  bool Is64Bit = false;
  return llvm::VCSIR::parseTuneCPU(Name, Is64Bit);
}

void VCSIRTargetInfo::fillValidTuneCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  bool Is64Bit = false;
  llvm::VCSIR::fillValidTuneCPUArchList(Values, Is64Bit);
}

ParsedTargetAttr VCSIRTargetInfo::parseTargetAttr(StringRef Features) const {
  return ParsedTargetAttr();
}

uint64_t VCSIRTargetInfo::getFMVPriority(ArrayRef<StringRef> Features) const {
  // Default Priority is zero.
  return 0;
}

bool VCSIRTargetInfo::validateCpuSupports(StringRef Feature) const {
  // Only allow extensions we have a known bit position for in the
  // __vcsir_feature_bits structure.
  return true;
}

bool VCSIRTargetInfo::isValidFeatureName(StringRef Name) const {
  return false;
}

bool VCSIRTargetInfo::validateGlobalRegisterVariable(
    StringRef RegName, unsigned RegSize, bool &HasSizeMismatch) const {
  if (RegName == "ra" || RegName == "sp" || RegName == "gp" ||
      RegName == "tp" || RegName.starts_with("x") || RegName.starts_with("a") ||
      RegName.starts_with("s") || RegName.starts_with("t")) {
    unsigned XLen = getTriple().isArch64Bit() ? 64 : 32;
    HasSizeMismatch = RegSize != XLen;
    return true;
  }
  return false;
}

bool VCSIRTargetInfo::validateCpuIs(StringRef CPUName) const {
  assert(getTriple().isOSLinux() &&
         "__builtin_cpu_is() is only supported for Linux.");

  return llvm::VCSIR::hasValidCPUModel(CPUName);
}
