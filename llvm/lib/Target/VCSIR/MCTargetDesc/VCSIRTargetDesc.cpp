//===-- VCSIRMCTargetDesc.cpp - VCSIR Target Descriptions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file provides VCSIR specific target descriptions.
///
//===----------------------------------------------------------------------===//

#include "VCSIRTargetDesc.h"
#include "TargetInfo/VCSIRTargetInfo.h"
#include "VCSIRELFStreamer.h"
#include "VCSIRInstPrinter.h"
#include "VCSIRMCAsmInfo.h"
#include "VCSIRMCObjectFileInfo.h"
#include "VCSIRTargetStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include <bitset>

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "VCSIRGenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "VCSIRGenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "VCSIRGenSubtargetInfo.inc"
using namespace llvm;

static MCRegisterInfo *createVCSIRMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitVCSIRMCRegisterInfo(X, VCSIR::X1);
  return X;
}

static MCInstrInfo *createVCSIRMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitVCSIRMCInstrInfo(X);
  return X;
}

static MCSubtargetInfo *
createVCSIRMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createVCSIRMCSubtargetInfoImpl(TT, CPU, /*TheVCSIRTargetuneCPU*/ CPU, FS);
}

static MCAsmInfo *createVCSIRMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new VCSIRELFMCAsmInfo(TT);
  unsigned SP = MRI.getDwarfRegNum(VCSIR::X1, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

static MCInstPrinter *createVCSIRInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  return new VCSIRInstPrinter(MAI, MII, MRI);
}

namespace {

class VCSIRMCInstrAnalysis : public MCInstrAnalysis {
  int64_t GPRState[31] = {};
  std::bitset<31> GPRValidMask;

  static bool isGPR(MCRegister Reg) {
    return Reg >= VCSIR::X0 && Reg <= VCSIR::X31;
  }

  static unsigned getRegIndex(MCRegister Reg) {
    assert(isGPR(Reg) && Reg != VCSIR::X0 && "Invalid GPR reg");
    return Reg - VCSIR::X1;
  }

  void setGPRState(MCRegister Reg, std::optional<int64_t> Value) {
    if (Reg == VCSIR::X0)
      return;

    auto Index = getRegIndex(Reg);

    if (Value) {
      GPRState[Index] = *Value;
      GPRValidMask.set(Index);
    } else {
      GPRValidMask.reset(Index);
    }
  }

  std::optional<int64_t> getGPRState(MCRegister Reg) const {
    if (Reg == VCSIR::X0)
      return 0;

    auto Index = getRegIndex(Reg);

    if (GPRValidMask.test(Index))
      return GPRState[Index];
    return std::nullopt;
  }

public:
  explicit VCSIRMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  void resetState() override { GPRValidMask.reset(); }

  void updateState(const MCInst &Inst, uint64_t Addr) override {
    // Terminators mark the end of a basic block which means the sequentially
    // next instruction will be the first of another basic block and the current
    // state will typically not be valid anymore. For calls, we assume all
    // registers may be clobbered by the callee (TODO: should we take the
    // calling convention into account?).
    if (isTerminator(Inst) || isCall(Inst)) {
      resetState();
      return;
    }

    switch (Inst.getOpcode()) {
    default: {
      // Clear the state of all defined registers for instructions that we don't
      // explicitly support.
      auto NumDefs = Info->get(Inst.getOpcode()).getNumDefs();
      for (unsigned I = 0; I < NumDefs; ++I) {
        auto DefReg = Inst.getOperand(I).getReg();
        if (isGPR(DefReg))
          setGPRState(DefReg, std::nullopt);
      }
      break;
    }
    case VCSIR::AUIPC:
      setGPRState(Inst.getOperand(0).getReg(),
                  Addr + (Inst.getOperand(1).getImm() << 12));
      break;
    }
  }

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    if (isConditionalBranch(Inst)) {
      int64_t Imm;
      if (Size == 2)
        Imm = Inst.getOperand(1).getImm();
      else
        Imm = Inst.getOperand(2).getImm();
      Target = Addr + Imm;
      return true;
    }

    if (Inst.getOpcode() == VCSIR::JAL) {
      Target = Addr + Inst.getOperand(1).getImm();
      return true;
    }

    if (Inst.getOpcode() == VCSIR::JALR) {
      if (auto TargetRegState = getGPRState(Inst.getOperand(1).getReg())) {
        Target = *TargetRegState + Inst.getOperand(2).getImm();
        return true;
      }

      return false;
    }

    return false;
  }

  bool isTerminator(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isTerminator(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case VCSIR::JAL:
    case VCSIR::JALR:
      return Inst.getOperand(0).getReg() == VCSIR::X0;
    }
  }

  bool isCall(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isCall(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case VCSIR::JAL:
    case VCSIR::JALR:
      return Inst.getOperand(0).getReg() != VCSIR::X0;
    }
  }

  bool isReturn(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isReturn(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case VCSIR::JALR:
      return Inst.getOperand(0).getReg() == VCSIR::X0 &&
             maybeReturnAddress(Inst.getOperand(1).getReg());
    }
  }

  bool isBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isBranch(Inst))
      return true;

    return isBranchImpl(Inst);
  }

  bool isUnconditionalBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isUnconditionalBranch(Inst))
      return true;

    return isBranchImpl(Inst);
  }

  bool isIndirectBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isIndirectBranch(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case VCSIR::JALR:
      return Inst.getOperand(0).getReg() == VCSIR::X0 &&
             !maybeReturnAddress(Inst.getOperand(1).getReg());
    }
  }

private:
  static bool maybeReturnAddress(MCRegister Reg) {
    // X1 is used for normal returns, X5 for returns from outlined functions.
    return Reg == VCSIR::X1 || Reg == VCSIR::X5;
  }

  static bool isBranchImpl(const MCInst &Inst) {
    switch (Inst.getOpcode()) {
    default:
      return false;
    case VCSIR::JAL:
      return Inst.getOperand(0).getReg() == VCSIR::X0;
    case VCSIR::JALR:
      return Inst.getOperand(0).getReg() == VCSIR::X0 &&
             !maybeReturnAddress(Inst.getOperand(1).getReg());
    }
  }
};
static MCObjectFileInfo *
createVCSIRMCObjectFileInfo(MCContext &Ctx, bool PIC,
                            bool LargeCodeModel = false) {
  MCObjectFileInfo *MOFI = new VCSIRMCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}
static MCInstPrinter *createVCSIRMCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new VCSIRInstPrinter(MAI, MII, MRI);
}
static MCTargetStreamer *
createVCSIRObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new VCSIRTargetELFStreamer(S, STI);
  return nullptr;
}

static MCTargetStreamer *
createVCSIRAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                             MCInstPrinter *InstPrint) {
  return new VCSIRTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createVCSIRNullTargetStreamer(MCStreamer &S) {
  return new VCSIRTargetStreamer(S);
}
static MCInstrAnalysis *createVCSIRInstrAnalysis(const MCInstrInfo *Info) {
  return new VCSIRMCInstrAnalysis(Info);
}
} // end anonymous namespace

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVCSIRTargetMC() {
  Target &TheVCSIRTarget = getTheVCSIRTarget();
    TargetRegistry::RegisterMCAsmInfo(TheVCSIRTarget, createVCSIRMCAsmInfo);
    TargetRegistry::RegisterMCObjectFileInfo(TheVCSIRTarget, createVCSIRMCObjectFileInfo);
    TargetRegistry::RegisterMCInstrInfo(TheVCSIRTarget, createVCSIRMCInstrInfo);
    TargetRegistry::RegisterMCRegInfo(TheVCSIRTarget, createVCSIRMCRegisterInfo);
    TargetRegistry::RegisterMCAsmBackend(TheVCSIRTarget, createVCSIRAsmBackend);
    TargetRegistry::RegisterMCCodeEmitter(TheVCSIRTarget, createVCSIRMCCodeEmitter);
    TargetRegistry::RegisterMCInstPrinter(TheVCSIRTarget, createVCSIRMCInstPrinter);
    TargetRegistry::RegisterMCSubtargetInfo(TheVCSIRTarget, createVCSIRMCSubtargetInfo);
    TargetRegistry::RegisterELFStreamer(TheVCSIRTarget, createVCSIRELFStreamer);
    TargetRegistry::RegisterObjectTargetStreamer(
        TheVCSIRTarget, createVCSIRObjectTargetStreamer);
    TargetRegistry::RegisterMCInstrAnalysis(TheVCSIRTarget, createVCSIRInstrAnalysis);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(TheVCSIRTarget, createVCSIRAsmTargetStreamer);
    // Register the null target streamer.
    TargetRegistry::RegisterNullTargetStreamer(TheVCSIRTarget,
                                               createVCSIRNullTargetStreamer);
}
