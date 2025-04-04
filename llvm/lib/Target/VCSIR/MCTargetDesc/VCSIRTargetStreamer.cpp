//===-- VCSIRTargetStreamer.cpp - VCSIR Target Streamer Methods ----------===//
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

#include "VCSIRTargetStreamer.h"
#include "VCSIRBaseInfo.h"
#include "VCSIRTargetDesc.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

VCSIRTargetStreamer::VCSIRTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

void VCSIRTargetStreamer::finish() { finishAttributeSection(); }
void VCSIRTargetStreamer::reset() {}

void VCSIRTargetStreamer::emitDirectiveOptionPush() {}
void VCSIRTargetStreamer::emitDirectiveOptionPop() {}
void VCSIRTargetStreamer::emitDirectiveOptionPIC() {}
void VCSIRTargetStreamer::emitDirectiveOptionNoPIC() {}
void VCSIRTargetStreamer::emitDirectiveOptionRVC() {}
void VCSIRTargetStreamer::emitDirectiveOptionNoRVC() {}
void VCSIRTargetStreamer::emitDirectiveOptionRelax() {}
void VCSIRTargetStreamer::emitDirectiveOptionNoRelax() {}
void VCSIRTargetStreamer::emitDirectiveOptionArch(
    ArrayRef<VCSIROptionArchArg> Args) {}
void VCSIRTargetStreamer::emitDirectiveVariantCC(MCSymbol &Symbol) {}
void VCSIRTargetStreamer::emitAttribute(unsigned Attribute, unsigned Value) {}
void VCSIRTargetStreamer::finishAttributeSection() {}
void VCSIRTargetStreamer::emitTextAttribute(unsigned Attribute,
                                            StringRef String) {}
void VCSIRTargetStreamer::emitIntTextAttribute(unsigned Attribute,
                                               unsigned IntValue,
                                               StringRef StringValue) {}

void VCSIRTargetStreamer::emitTargetAttributes(const MCSubtargetInfo &STI,
                                               bool EmitStackAlign) {}

// This part is for ascii assembly output
VCSIRTargetAsmStreamer::VCSIRTargetAsmStreamer(MCStreamer &S,
                                               formatted_raw_ostream &OS)
    : VCSIRTargetStreamer(S), OS(OS) {}

void VCSIRTargetAsmStreamer::emitDirectiveOptionPush() {
  OS << "\t.option\tpush\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionPop() {
  OS << "\t.option\tpop\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionPIC() {
  OS << "\t.option\tpic\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionNoPIC() {
  OS << "\t.option\tnopic\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionRVC() {
  OS << "\t.option\trvc\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionNoRVC() {
  OS << "\t.option\tnorvc\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionRelax() {
  OS << "\t.option\trelax\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionNoRelax() {
  OS << "\t.option\tnorelax\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveOptionArch(
    ArrayRef<VCSIROptionArchArg> Args) {
  OS << "\t.option\tarch";
  for (const auto &Arg : Args) {
    OS << ", ";
    switch (Arg.Type) {
    case VCSIROptionArchArgType::Full:
      break;
    case VCSIROptionArchArgType::Plus:
      OS << "+";
      break;
    case VCSIROptionArchArgType::Minus:
      OS << "-";
      break;
    }
    OS << Arg.Value;
  }
  OS << "\n";
}

void VCSIRTargetAsmStreamer::emitDirectiveVariantCC(MCSymbol &Symbol) {
  OS << "\t.variant_cc\t" << Symbol.getName() << "\n";
}

void VCSIRTargetAsmStreamer::emitAttribute(unsigned Attribute, unsigned Value) {
  OS << "\t.attribute\t" << Attribute << ", " << Twine(Value) << "\n";
}

void VCSIRTargetAsmStreamer::emitTextAttribute(unsigned Attribute,
                                               StringRef String) {
  OS << "\t.attribute\t" << Attribute << ", \"" << String << "\"\n";
}

void VCSIRTargetAsmStreamer::emitIntTextAttribute(unsigned Attribute,
                                                  unsigned IntValue,
                                                  StringRef StringValue) {}

void VCSIRTargetAsmStreamer::finishAttributeSection() {}
