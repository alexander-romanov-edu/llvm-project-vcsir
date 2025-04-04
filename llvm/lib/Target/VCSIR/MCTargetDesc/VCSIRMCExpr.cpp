//===-- VCSIRMCExpr.cpp - VCSIR specific MC expression classes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the assembly expression modifiers
// accepted by the VCSIR architecture (e.g. ":lo12:", ":gottprel_g1:", ...).
//
//===----------------------------------------------------------------------===//

#include "VCSIRMCExpr.h"
#include "MCTargetDesc/VCSIRAsmBackend.h"
#include "VCSIRFixupKinds.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "vcsirmcexpr"

const VCSIRMCExpr *VCSIRMCExpr::create(const MCExpr *Expr, VariantKind Kind,
                                       MCContext &Ctx) {
  return new (Ctx) VCSIRMCExpr(Expr, Kind);
}

void VCSIRMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  VariantKind Kind = getKind();
  bool HasVariant = ((Kind != VK_VCSIR_None) && (Kind != VK_VCSIR_CALL) &&
                     (Kind != VK_VCSIR_CALL_PLT));

  if (HasVariant)
    OS << '%' << getVariantKindName(getKind()) << '(';
  Expr->print(OS, MAI);
  if (HasVariant)
    OS << ')';
}

const MCFixup *VCSIRMCExpr::getPCRelHiFixup(const MCFragment **DFOut) const {
  MCValue AUIPCLoc;
  if (!getSubExpr()->evaluateAsRelocatable(AUIPCLoc, nullptr, nullptr))
    return nullptr;

  const MCSymbolRefExpr *AUIPCSRE = AUIPCLoc.getSymA();
  if (!AUIPCSRE)
    return nullptr;

  const MCSymbol *AUIPCSymbol = &AUIPCSRE->getSymbol();
  const auto *DF = dyn_cast_or_null<MCDataFragment>(AUIPCSymbol->getFragment());

  if (!DF)
    return nullptr;

  uint64_t Offset = AUIPCSymbol->getOffset();
  if (DF->getContents().size() == Offset) {
    DF = dyn_cast_or_null<MCDataFragment>(DF->getNext());
    if (!DF)
      return nullptr;
    Offset = 0;
  }

  for (const MCFixup &F : DF->getFixups()) {
    if (F.getOffset() != Offset)
      continue;

    switch ((unsigned)F.getKind()) {
    default:
      continue;
    case VCSIR::fixup_vcsir_got_hi20:
    case VCSIR::fixup_vcsir_tls_got_hi20:
    case VCSIR::fixup_vcsir_tls_gd_hi20:
    case VCSIR::fixup_vcsir_pcrel_hi20:
    case VCSIR::fixup_vcsir_tlsdesc_hi20:
      if (DFOut)
        *DFOut = DF;
      return &F;
    }
  }

  return nullptr;
}

bool VCSIRMCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                            const MCAssembler *Asm,
                                            const MCFixup *Fixup) const {
  // Explicitly drop the layout and assembler to prevent any symbolic folding in
  // the expression handling.  This is required to preserve symbolic difference
  // expressions to emit the paired relocations.
  if (!getSubExpr()->evaluateAsRelocatable(Res, nullptr, nullptr))
    return false;

  Res =
      MCValue::get(Res.getSymA(), Res.getSymB(), Res.getConstant(), getKind());
  // Custom fixup types are not valid with symbol difference expressions.
  return Res.getSymB() ? getKind() == VK_VCSIR_None : true;
}

void VCSIRMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

VCSIRMCExpr::VariantKind VCSIRMCExpr::getVariantKindForName(StringRef name) {
  return StringSwitch<VCSIRMCExpr::VariantKind>(name)
      .Case("lo", VK_VCSIR_LO)
      .Case("hi", VK_VCSIR_HI)
      .Case("pcrel_lo", VK_VCSIR_PCREL_LO)
      .Case("pcrel_hi", VK_VCSIR_PCREL_HI)
      .Case("got_pcrel_hi", VK_VCSIR_GOT_HI)
      .Case("tprel_lo", VK_VCSIR_TPREL_LO)
      .Case("tprel_hi", VK_VCSIR_TPREL_HI)
      .Case("tprel_add", VK_VCSIR_TPREL_ADD)
      .Case("tls_ie_pcrel_hi", VK_VCSIR_TLS_GOT_HI)
      .Case("tls_gd_pcrel_hi", VK_VCSIR_TLS_GD_HI)
      .Case("tlsdesc_hi", VK_VCSIR_TLSDESC_HI)
      .Case("tlsdesc_load_lo", VK_VCSIR_TLSDESC_LOAD_LO)
      .Case("tlsdesc_add_lo", VK_VCSIR_TLSDESC_ADD_LO)
      .Case("tlsdesc_call", VK_VCSIR_TLSDESC_CALL)
      .Default(VK_VCSIR_Invalid);
}

StringRef VCSIRMCExpr::getVariantKindName(VariantKind Kind) {
  switch (Kind) {
  case VK_VCSIR_Invalid:
  case VK_VCSIR_None:
    llvm_unreachable("Invalid ELF symbol kind");
  case VK_VCSIR_LO:
    return "lo";
  case VK_VCSIR_HI:
    return "hi";
  case VK_VCSIR_PCREL_LO:
    return "pcrel_lo";
  case VK_VCSIR_PCREL_HI:
    return "pcrel_hi";
  case VK_VCSIR_GOT_HI:
    return "got_pcrel_hi";
  case VK_VCSIR_TPREL_LO:
    return "tprel_lo";
  case VK_VCSIR_TPREL_HI:
    return "tprel_hi";
  case VK_VCSIR_TPREL_ADD:
    return "tprel_add";
  case VK_VCSIR_TLS_GOT_HI:
    return "tls_ie_pcrel_hi";
  case VK_VCSIR_TLSDESC_HI:
    return "tlsdesc_hi";
  case VK_VCSIR_TLSDESC_LOAD_LO:
    return "tlsdesc_load_lo";
  case VK_VCSIR_TLSDESC_ADD_LO:
    return "tlsdesc_add_lo";
  case VK_VCSIR_TLSDESC_CALL:
    return "tlsdesc_call";
  case VK_VCSIR_TLS_GD_HI:
    return "tls_gd_pcrel_hi";
  case VK_VCSIR_CALL:
    return "call";
  case VK_VCSIR_CALL_PLT:
    return "call_plt";
  case VK_VCSIR_32_PCREL:
    return "32_pcrel";
  }
  llvm_unreachable("Invalid ELF symbol kind");
}

static void fixELFSymbolsInTLSFixupsImpl(const MCExpr *Expr, MCAssembler &Asm) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    llvm_unreachable("Can't handle nested target expression");
    break;
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(Expr);
    fixELFSymbolsInTLSFixupsImpl(BE->getLHS(), Asm);
    fixELFSymbolsInTLSFixupsImpl(BE->getRHS(), Asm);
    break;
  }

  case MCExpr::SymbolRef: {
    // We're known to be under a TLS fixup, so any symbol should be
    // modified. There should be only one.
    const MCSymbolRefExpr &SymRef = *cast<MCSymbolRefExpr>(Expr);
    cast<MCSymbolELF>(SymRef.getSymbol()).setType(ELF::STT_TLS);
    break;
  }

  case MCExpr::Unary:
    fixELFSymbolsInTLSFixupsImpl(cast<MCUnaryExpr>(Expr)->getSubExpr(), Asm);
    break;
  }
}

void VCSIRMCExpr::fixELFSymbolsInTLSFixups(MCAssembler &Asm) const {
  switch (getKind()) {
  default:
    return;
  case VK_VCSIR_TPREL_HI:
  case VK_VCSIR_TLS_GOT_HI:
  case VK_VCSIR_TLS_GD_HI:
  case VK_VCSIR_TLSDESC_HI:
    break;
  }

  fixELFSymbolsInTLSFixupsImpl(getSubExpr(), Asm);
}

bool VCSIRMCExpr::evaluateAsConstant(int64_t &Res) const {
  MCValue Value;

  if (Kind == VK_VCSIR_PCREL_HI || Kind == VK_VCSIR_PCREL_LO ||
      Kind == VK_VCSIR_GOT_HI || Kind == VK_VCSIR_TPREL_HI ||
      Kind == VK_VCSIR_TPREL_LO || Kind == VK_VCSIR_TPREL_ADD ||
      Kind == VK_VCSIR_TLS_GOT_HI || Kind == VK_VCSIR_TLS_GD_HI ||
      Kind == VK_VCSIR_TLSDESC_HI || Kind == VK_VCSIR_TLSDESC_LOAD_LO ||
      Kind == VK_VCSIR_TLSDESC_ADD_LO || Kind == VK_VCSIR_TLSDESC_CALL ||
      Kind == VK_VCSIR_CALL || Kind == VK_VCSIR_CALL_PLT)
    return false;

  if (!getSubExpr()->evaluateAsRelocatable(Value, nullptr, nullptr))
    return false;

  if (!Value.isAbsolute())
    return false;

  Res = evaluateAsInt64(Value.getConstant());
  return true;
}

int64_t VCSIRMCExpr::evaluateAsInt64(int64_t Value) const {
  switch (Kind) {
  default:
    llvm_unreachable("Invalid kind");
  case VK_VCSIR_LO:
    return SignExtend64<12>(Value);
  case VK_VCSIR_HI:
    // Add 1 if bit 11 is 1, to compensate for low 12 bits being negative.
    return ((Value + 0x800) >> 12) & 0xfffff;
  }
}
