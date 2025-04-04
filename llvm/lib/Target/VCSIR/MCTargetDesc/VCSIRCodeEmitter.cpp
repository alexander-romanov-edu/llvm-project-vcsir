//===-- VCSIRMCCodeEmitter.cpp - Convert VCSIR code to machine code ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the VCSIRMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/VCSIRBaseInfo.h"
#include "MCTargetDesc/VCSIRFixupKinds.h"
#include "MCTargetDesc/VCSIRMCExpr.h"
#include "MCTargetDesc/VCSIRTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");
STATISTIC(MCNumFixups, "Number of MC fixups created");

namespace {
class VCSIRMCCodeEmitter : public MCCodeEmitter {
  VCSIRMCCodeEmitter(const VCSIRMCCodeEmitter &) = delete;
  void operator=(const VCSIRMCCodeEmitter &) = delete;
  MCContext &Ctx;
  MCInstrInfo const &MCII;

public:
  VCSIRMCCodeEmitter(MCContext &ctx, MCInstrInfo const &MCII)
      : Ctx(ctx), MCII(MCII) {}

  ~VCSIRMCCodeEmitter() override = default;

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  void expandFunctionCall(const MCInst &MI, SmallVectorImpl<char> &CB,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  void expandAddTPRel(const MCInst &MI, SmallVectorImpl<char> &CB,
                      SmallVectorImpl<MCFixup> &Fixups,
                      const MCSubtargetInfo &STI) const;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// Return binary encoding of operand. If the machine operand requires
  /// relocation, record the relocation and return zero.
  uint64_t getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  uint64_t getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  uint64_t getImmOpValue(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  unsigned getVMaskReg(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const;

  unsigned getRlistOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  unsigned getRegReg(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const;
};
} // end anonymous namespace

MCCodeEmitter *llvm::createVCSIRMCCodeEmitter(const MCInstrInfo &MCII,
                                              MCContext &Ctx) {
  return new VCSIRMCCodeEmitter(Ctx, MCII);
}

// Expand PseudoCALL(Reg), PseudoTAIL and PseudoJump to AUIPC and JALR with
// relocation types. We expand those pseudo-instructions while encoding them,
// meaning AUIPC and JALR won't go through VCSIR MC to MC compressed
// instruction transformation. This is acceptable because AUIPC has no 16-bit
// form and C_JALR has no immediate operand field.  We let linker relaxation
// deal with it. When linker relaxation is enabled, AUIPC and JALR have a
// chance to relax to JAL.
// If the C extension is enabled, JAL has a chance relax to C_JAL.
void VCSIRMCCodeEmitter::expandFunctionCall(const MCInst &MI,
                                            SmallVectorImpl<char> &CB,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  MCInst TmpInst;
  MCOperand Func;
  MCRegister Ra;
  if (MI.getOpcode() == VCSIR::PseudoCALL) {
    Func = MI.getOperand(0);
    Ra = VCSIR::X1;
  } else if (MI.getOpcode() == VCSIR::PseudoJump) {
    Func = MI.getOperand(1);
    Ra = MI.getOperand(0).getReg();
  }
  uint32_t Binary;

  assert(Func.isExpr() && "Expected expression");

  const MCExpr *CallExpr = Func.getExpr();

  // Emit AUIPC Ra, Func with R_VCSIR_CALL relocation type.
  TmpInst = MCInstBuilder(VCSIR::AUIPC).addReg(Ra).addExpr(CallExpr);
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);

  if (MI.getOpcode() == VCSIR::PseudoJump)
    // Emit JALR X0, Ra, 0
    TmpInst = MCInstBuilder(VCSIR::JALR).addReg(VCSIR::X0).addReg(Ra).addImm(0);
  else
    // Emit JALR Ra, Ra, 0
    TmpInst = MCInstBuilder(VCSIR::JALR).addReg(Ra).addReg(Ra).addImm(0);
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);
}

// Expand PseudoAddTPRel to a simple ADD with the correct relocation.
void VCSIRMCCodeEmitter::expandAddTPRel(const MCInst &MI,
                                        SmallVectorImpl<char> &CB,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  MCOperand DestReg = MI.getOperand(0);
  MCOperand SrcReg = MI.getOperand(1);
  MCOperand TPReg = MI.getOperand(2);
  assert(TPReg.isReg() && TPReg.getReg() == VCSIR::X4 &&
         "Expected thread pointer as second input to TP-relative add");

  MCOperand SrcSymbol = MI.getOperand(3);
  assert(SrcSymbol.isExpr() &&
         "Expected expression as third input to TP-relative add");

  // Emit a normal ADD instruction with the given operands.
  MCInst TmpInst = MCInstBuilder(VCSIR::ADD)
                       .addOperand(DestReg)
                       .addOperand(SrcReg)
                       .addOperand(TPReg);
  uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(CB, Binary, llvm::endianness::little);
}

void VCSIRMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  // Get byte count of instruction.
  unsigned Size = Desc.getSize();

  // VCSIRInstrInfo::getInstSizeInBytes expects that the total size of the
  // expanded instructions for each pseudo is correct in the Size field of the
  // tablegen definition for the pseudo.
  switch (MI.getOpcode()) {
  default:
    break;
  case VCSIR::PseudoCALL:
  case VCSIR::PseudoJump:
    expandFunctionCall(MI, CB, Fixups, STI);
    MCNumEmitted += 2;
    return;
  }

  switch (Size) {
  default:
    llvm_unreachable("Unhandled encodeInstruction length!");
  case 2: {
    uint16_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write<uint16_t>(CB, Bits, llvm::endianness::little);
    break;
  }
  case 4: {
    uint32_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write(CB, Bits, llvm::endianness::little);
    break;
  }
  case 6: {
    uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI) & 0xffff'ffff'ffffu;
    SmallVector<char, 8> Encoding;
    support::endian::write(Encoding, Bits, llvm::endianness::little);
    assert(Encoding[6] == 0 && Encoding[7] == 0 &&
           "Unexpected encoding for 48-bit instruction");
    Encoding.truncate(6);
    CB.append(Encoding);
    break;
  }
  case 8: {
    uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write(CB, Bits, llvm::endianness::little);
    break;
  }
  }

  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}

uint64_t
VCSIRMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {

  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  if (MO.isImm())
    return MO.getImm();

  llvm_unreachable("Unhandled expression!");
  return 0;
}

uint64_t
VCSIRMCCodeEmitter::getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) {
    uint64_t Res = MO.getImm();
    assert((Res & 1) == 0 && "LSB is non-zero");
    return Res >> 1;
  }

  return getImmOpValue(MI, OpNo, Fixups, STI);
}

uint64_t VCSIRMCCodeEmitter::getImmOpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  bool EnableRelax = false;
  const MCOperand &MO = MI.getOperand(OpNo);

  MCInstrDesc const &Desc = MCII.get(MI.getOpcode());
  unsigned MIFrm = VCSIRII::getFormat(Desc.TSFlags);

  // If the destination is an immediate, there is nothing to do.
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() && "getImmOpValue expects only expressions or immediates");
  const MCExpr *Expr = MO.getExpr();
  MCExpr::ExprKind Kind = Expr->getKind();
  VCSIR::Fixups FixupKind = VCSIR::fixup_vcsir_invalid;
  bool RelaxCandidate = false;
  if (Kind == MCExpr::Target) {
    const VCSIRMCExpr *RVExpr = cast<VCSIRMCExpr>(Expr);

    switch (RVExpr->getKind()) {
    case VCSIRMCExpr::VK_VCSIR_None:
    case VCSIRMCExpr::VK_VCSIR_Invalid:
    case VCSIRMCExpr::VK_VCSIR_32_PCREL:
      llvm_unreachable("Unhandled fixup kind!");
    case VCSIRMCExpr::VK_VCSIR_TPREL_ADD:
      // tprel_add is only used to indicate that a relocation should be emitted
      // for an add instruction used in TP-relative addressing. It should not be
      // expanded as if representing an actual instruction operand and so to
      // encounter it here is an error.
      llvm_unreachable(
          "VK_VCSIR_TPREL_ADD should not represent an instruction operand");
    case VCSIRMCExpr::VK_VCSIR_LO:
      if (MIFrm == VCSIRII::InstFormatI)
        FixupKind = VCSIR::fixup_vcsir_lo12_i;
      else if (MIFrm == VCSIRII::InstFormatS)
        FixupKind = VCSIR::fixup_vcsir_lo12_s;
      else
        llvm_unreachable("VK_VCSIR_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_HI:
      FixupKind = VCSIR::fixup_vcsir_hi20;
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_PCREL_LO:
      if (MIFrm == VCSIRII::InstFormatI)
        FixupKind = VCSIR::fixup_vcsir_pcrel_lo12_i;
      else if (MIFrm == VCSIRII::InstFormatS)
        FixupKind = VCSIR::fixup_vcsir_pcrel_lo12_s;
      else
        llvm_unreachable(
            "VK_VCSIR_PCREL_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_PCREL_HI:
      FixupKind = VCSIR::fixup_vcsir_pcrel_hi20;
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_GOT_HI:
      FixupKind = VCSIR::fixup_vcsir_got_hi20;
      break;
    case VCSIRMCExpr::VK_VCSIR_TPREL_LO:
      if (MIFrm == VCSIRII::InstFormatI)
        FixupKind = VCSIR::fixup_vcsir_tprel_lo12_i;
      else if (MIFrm == VCSIRII::InstFormatS)
        FixupKind = VCSIR::fixup_vcsir_tprel_lo12_s;
      else
        llvm_unreachable(
            "VK_VCSIR_TPREL_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_TPREL_HI:
      FixupKind = VCSIR::fixup_vcsir_tprel_hi20;
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_TLS_GOT_HI:
      FixupKind = VCSIR::fixup_vcsir_tls_got_hi20;
      break;
    case VCSIRMCExpr::VK_VCSIR_TLS_GD_HI:
      FixupKind = VCSIR::fixup_vcsir_tls_gd_hi20;
      break;
    case VCSIRMCExpr::VK_VCSIR_CALL:
      FixupKind = VCSIR::fixup_vcsir_call;
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_CALL_PLT:
      FixupKind = VCSIR::fixup_vcsir_call_plt;
      RelaxCandidate = true;
      break;
    case VCSIRMCExpr::VK_VCSIR_TLSDESC_HI:
      FixupKind = VCSIR::fixup_vcsir_tlsdesc_hi20;
      break;
    case VCSIRMCExpr::VK_VCSIR_TLSDESC_LOAD_LO:
      FixupKind = VCSIR::fixup_vcsir_tlsdesc_load_lo12;
      break;
    case VCSIRMCExpr::VK_VCSIR_TLSDESC_ADD_LO:
      FixupKind = VCSIR::fixup_vcsir_tlsdesc_add_lo12;
      break;
    case VCSIRMCExpr::VK_VCSIR_TLSDESC_CALL:
      FixupKind = VCSIR::fixup_vcsir_tlsdesc_call;
      break;
    }
  } else if ((Kind == MCExpr::SymbolRef &&
              cast<MCSymbolRefExpr>(Expr)->getKind() ==
                  MCSymbolRefExpr::VK_None) ||
             Kind == MCExpr::Binary) {
    // FIXME: Sub kind binary exprs have chance of underflow.
    if (MIFrm == VCSIRII::InstFormatJ) {
      FixupKind = VCSIR::fixup_vcsir_jal;
    } else if (MIFrm == VCSIRII::InstFormatB) {
      FixupKind = VCSIR::fixup_vcsir_branch;
    } else if (MIFrm == VCSIRII::InstFormatCJ) {
      FixupKind = VCSIR::fixup_vcsir_rvc_jump;
    } else if (MIFrm == VCSIRII::InstFormatCB) {
      FixupKind = VCSIR::fixup_vcsir_rvc_branch;
    } else if (MIFrm == VCSIRII::InstFormatI) {
      FixupKind = VCSIR::fixup_vcsir_12_i;
    }
  }

  assert(FixupKind != VCSIR::fixup_vcsir_invalid && "Unhandled expression!");

  Fixups.push_back(
      MCFixup::create(0, Expr, MCFixupKind(FixupKind), MI.getLoc()));
  ++MCNumFixups;

  // Ensure an R_VCSIR_RELAX relocation will be emitted if linker relaxation is
  // enabled and the current fixup will result in a relocation that may be
  // relaxed.
  if (EnableRelax && RelaxCandidate) {
    const MCConstantExpr *Dummy = MCConstantExpr::create(0, Ctx);
    Fixups.push_back(MCFixup::create(
        0, Dummy, MCFixupKind(VCSIR::fixup_vcsir_relax), MI.getLoc()));
    ++MCNumFixups;
  }

  return 0;
}

unsigned VCSIRMCCodeEmitter::getVMaskReg(const MCInst &MI, unsigned OpNo,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  MCOperand MO = MI.getOperand(OpNo);
  assert(MO.isReg() && "Expected a register.");

  switch (MO.getReg()) {
  default:
    llvm_unreachable("Invalid mask register.");
  case VCSIR::NoRegister:
    return 1;
  }
}

unsigned VCSIRMCCodeEmitter::getRlistOpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm() && "Rlist operand must be immediate");
  auto Imm = MO.getImm();
  assert(Imm >= 4 && "EABI is currently not implemented");
  return Imm;
}

unsigned VCSIRMCCodeEmitter::getRegReg(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  const MCOperand &MO1 = MI.getOperand(OpNo + 1);
  assert(MO.isReg() && MO1.isReg() && "Expected registers.");

  unsigned Op = Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
  unsigned Op1 = Ctx.getRegisterInfo()->getEncodingValue(MO1.getReg());

  return Op | Op1 << 5;
}

#include "VCSIRGenMCCodeEmitter.inc"
