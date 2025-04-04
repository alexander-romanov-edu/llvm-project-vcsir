//===-- VCSIRELFObjectWriter.cpp - VCSIR ELF Writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VCSIRELFObjectWriter.h"
#include "llvm/MC/MCELFObjectWriter.h"

#include <memory>

namespace llvm {
namespace {
class VCSIRELFObjectWriter : public MCELFObjectTargetWriter {
public:
  VCSIRELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit*/ false, OSABI, ELF::EM_VCSIR,
                                /*HasRelocationAddend*/ true) {}

  // Return true if the given relocation must be with a symbol rather than
  // section plus offset.
  bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                               unsigned Type) const override {
    return true;
  }

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override {
      return ELF::R_VCSIR_NONE;
  }
};
} // namespace

std::unique_ptr<MCObjectTargetWriter>
createVCSIRELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<VCSIRELFObjectWriter>(OSABI);
}

} // namespace llvm
