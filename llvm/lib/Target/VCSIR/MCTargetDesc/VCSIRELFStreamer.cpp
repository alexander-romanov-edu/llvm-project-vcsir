//===-- VCSIRELFStreamer.cpp - VCSIR ELF Target Streamer Methods ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides VCSIR specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "VCSIRELFStreamer.h"
#include "VCSIRAsmBackend.h"
#include "VCSIRBaseInfo.h"
#include "VCSIRTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"

using namespace llvm;

// This part is for ELF object output.
VCSIRTargetELFStreamer::VCSIRTargetELFStreamer(MCStreamer &S,
                                               const MCSubtargetInfo &STI)
    : VCSIRTargetStreamer(S), CurrentVendor("vcsir") {
  setFlagsFromFeatures(STI);
}

VCSIRELFStreamer &VCSIRTargetELFStreamer::getStreamer() {
  return static_cast<VCSIRELFStreamer &>(Streamer);
}

void VCSIRTargetELFStreamer::emitDirectiveOptionPush() {}
void VCSIRTargetELFStreamer::emitDirectiveOptionPop() {}
void VCSIRTargetELFStreamer::emitDirectiveOptionPIC() {}
void VCSIRTargetELFStreamer::emitDirectiveOptionNoPIC() {}
void VCSIRTargetELFStreamer::emitDirectiveOptionRVC() {}
void VCSIRTargetELFStreamer::emitDirectiveOptionNoRVC() {}
void VCSIRTargetELFStreamer::emitDirectiveOptionRelax() {}
void VCSIRTargetELFStreamer::emitDirectiveOptionNoRelax() {}

void VCSIRTargetELFStreamer::emitAttribute(unsigned Attribute, unsigned Value) {
  getStreamer().setAttributeItem(Attribute, Value, /*OverwriteExisting=*/true);
}

void VCSIRTargetELFStreamer::emitTextAttribute(unsigned Attribute,
                                               StringRef String) {
  getStreamer().setAttributeItem(Attribute, String, /*OverwriteExisting=*/true);
}

void VCSIRTargetELFStreamer::emitIntTextAttribute(unsigned Attribute,
                                                  unsigned IntValue,
                                                  StringRef StringValue) {
  getStreamer().setAttributeItems(Attribute, IntValue, StringValue,
                                  /*OverwriteExisting=*/true);
}

void VCSIRTargetELFStreamer::finishAttributeSection() {
  VCSIRELFStreamer &S = getStreamer();
  if (S.Contents.empty())
    return;
}

void VCSIRTargetELFStreamer::finish() {
  VCSIRTargetStreamer::finish();
  ELFObjectWriter &W = getStreamer().getWriter();
  unsigned EFlags = W.getELFHeaderEFlags();
  W.setELFHeaderEFlags(EFlags);
}

void VCSIRTargetELFStreamer::reset() { AttributeSection = nullptr; }

void VCSIRTargetELFStreamer::emitDirectiveVariantCC(MCSymbol &Symbol) {
  getStreamer().getAssembler().registerSymbol(Symbol);
}

void VCSIRELFStreamer::reset() {
  static_cast<VCSIRTargetStreamer *>(getTargetStreamer())->reset();
  MCELFStreamer::reset();
  LastMappingSymbols.clear();
  LastEMS = EMS_None;
}

void VCSIRELFStreamer::emitDataMappingSymbol() {
  if (LastEMS == EMS_Data)
    return;
  emitMappingSymbol("$d");
  LastEMS = EMS_Data;
}

void VCSIRELFStreamer::emitInstructionsMappingSymbol() {
  if (LastEMS == EMS_Instructions)
    return;
  emitMappingSymbol("$x");
  LastEMS = EMS_Instructions;
}

void VCSIRELFStreamer::emitMappingSymbol(StringRef Name) {
  auto *Symbol = cast<MCSymbolELF>(getContext().createLocalSymbol(Name));
  emitLabel(Symbol);
  Symbol->setType(ELF::STT_NOTYPE);
  Symbol->setBinding(ELF::STB_LOCAL);
}

void VCSIRELFStreamer::changeSection(MCSection *Section, uint32_t Subsection) {
  // We have to keep track of the mapping symbol state of any sections we
  // use. Each one should start off as EMS_None, which is provided as the
  // default constructor by DenseMap::lookup.
  LastMappingSymbols[getPreviousSection().first] = LastEMS;
  LastEMS = LastMappingSymbols.lookup(Section);

  MCELFStreamer::changeSection(Section, Subsection);
}

void VCSIRELFStreamer::emitInstruction(const MCInst &Inst,
                                       const MCSubtargetInfo &STI) {
  emitInstructionsMappingSymbol();
  MCELFStreamer::emitInstruction(Inst, STI);
}

void VCSIRELFStreamer::emitBytes(StringRef Data) {
  emitDataMappingSymbol();
  MCELFStreamer::emitBytes(Data);
}

void VCSIRELFStreamer::emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                                SMLoc Loc) {
  emitDataMappingSymbol();
  MCELFStreamer::emitFill(NumBytes, FillValue, Loc);
}

void VCSIRELFStreamer::emitValueImpl(const MCExpr *Value, unsigned Size,
                                     SMLoc Loc) {
  emitDataMappingSymbol();
  MCELFStreamer::emitValueImpl(Value, Size, Loc);
}

MCStreamer *llvm::createVCSIRELFStreamer(const Triple &, MCContext &C,
                                         std::unique_ptr<MCAsmBackend> &&MAB,
                                         std::unique_ptr<MCObjectWriter> &&MOW,
                                         std::unique_ptr<MCCodeEmitter> &&MCE) {
  return new VCSIRELFStreamer(C, std::move(MAB), std::move(MOW),
                              std::move(MCE));
}
