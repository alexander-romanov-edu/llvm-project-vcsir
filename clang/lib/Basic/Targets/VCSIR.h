//===--- VCSIR.h - Declare VCSIR target feature support --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares VCSIR TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_VCSIR_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_VCSIR_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace clang {
namespace targets {

// VCSIR Target
class VCSIRTargetInfo : public TargetInfo {
protected:
  std::string ABI, CPU;

private:
  bool FastScalarUnalignedAccess;
  bool HasExperimental = false;

public:
  VCSIRTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    BFloat16Width = 16;
    BFloat16Align = 16;
    BFloat16Format = &llvm::APFloat::BFloat();
    LongDoubleWidth = 128;
    LongDoubleAlign = 128;
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
    SuitableAlign = 128;
    WCharType = SignedInt;
    WIntType = UnsignedInt;
    MCountName = "_mcount";
    HasFloat16 = true;
    HasStrictFP = true;
    IntPtrType = SignedInt;
    PtrDiffType = SignedInt;
    SizeType = UnsignedInt;
    resetDataLayout("e-m:e-p:32:32-i64:64-n32-S128");
  }

  bool setCPU(const std::string &Name) override {
    if (!isValidCPUName(Name))
      return false;
    CPU = Name;
    return true;
  }

  StringRef getABI() const override { return ABI; }
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  std::string_view getClobbers() const override { return ""; }

  StringRef getConstraintRegister(StringRef Constraint,
                                  StringRef Expression) const override {
    return Expression;
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    if (RegNo == 0)
      return 10;
    else if (RegNo == 1)
      return 11;
    else
      return -1;
  }

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;

  std::string convertConstraint(const char *&Constraint) const override;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;

  std::optional<std::pair<unsigned, unsigned>>
  getVScaleRange(const LangOptions &LangOpts,
                 bool IsArmStreamingFunction) const override;

  bool hasFeature(StringRef Feature) const override;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  bool hasBitIntType() const override { return true; }

  bool hasBFloat16Type() const override { return true; }

  bool useFP16ConversionIntrinsics() const override {
    return false;
  }

  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;
  bool isValidTuneCPUName(StringRef Name) const override;
  void fillValidTuneCPUList(SmallVectorImpl<StringRef> &Values) const override;
  bool supportsTargetAttributeTune() const override { return true; }
  ParsedTargetAttr parseTargetAttr(StringRef Str) const override;
  uint64_t getFMVPriority(ArrayRef<StringRef> Features) const override;

  std::pair<unsigned, unsigned> hardwareInterferenceSizes() const override {
    return std::make_pair(32, 32);
  }

  bool supportsCpuSupports() const override { return getTriple().isOSLinux(); }
  bool supportsCpuIs() const override { return getTriple().isOSLinux(); }
  bool supportsCpuInit() const override { return getTriple().isOSLinux(); }
  bool validateCpuSupports(StringRef Feature) const override;
  bool validateCpuIs(StringRef CPUName) const override;
  bool isValidFeatureName(StringRef Name) const override;

  bool validateGlobalRegisterVariable(StringRef RegName, unsigned RegSize,
                                      bool &HasSizeMismatch) const override;

  bool checkCFProtectionBranchSupported(DiagnosticsEngine &) const override {
    // Always generate Zicfilp lpad insns
    // Non-zicfilp CPUs would read them as NOP
    return true;
  }

  bool
  checkCFProtectionReturnSupported(DiagnosticsEngine &Diags) const override {
    return TargetInfo::checkCFProtectionReturnSupported(Diags);
  }

  CFBranchLabelSchemeKind getDefaultCFBranchLabelScheme() const override {
    return CFBranchLabelSchemeKind::FuncSig;
  }

  bool
  checkCFBranchLabelSchemeSupported(const CFBranchLabelSchemeKind Scheme,
                                    DiagnosticsEngine &Diags) const override {
    switch (Scheme) {
    case CFBranchLabelSchemeKind::Default:
    case CFBranchLabelSchemeKind::Unlabeled:
    case CFBranchLabelSchemeKind::FuncSig:
      return true;
    }
    return TargetInfo::checkCFBranchLabelSchemeSupported(Scheme, Diags);
  }

  bool setABI(const std::string &Name) override {
    if (Name == "ilp32e") {
      ABI = Name;
      resetDataLayout("e-m:e-p:32:32-i64:64-n32-S32");
      return true;
    }

    if (Name == "ilp32" || Name == "ilp32f" || Name == "ilp32d") {
      ABI = Name;
      return true;
    }
    return false;
  }

  void setMaxAtomicWidth() override {
    MaxAtomicPromoteWidth = 1;
  }
};
} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_VCSIR_H
