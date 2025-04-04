//===-- VCSIRFrameLowering.cpp - VCSIR Frame Information -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the VCSIR implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "VCSIRFrameLowering.h"
#include "VCSIRMachineFunctionInfo.h"
#include "VCSIRSubtarget.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/LEB128.h"

#include <algorithm>

using namespace llvm;

namespace {

class CFISaveRegisterEmitter {
  MachineFunction &MF;
  MachineFrameInfo &MFI;

public:
  CFISaveRegisterEmitter(MachineFunction &MF)
      : MF{MF}, MFI{MF.getFrameInfo()} {};

  void emit(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
            const VCSIRRegisterInfo &RI, const VCSIRInstrInfo &TII,
            const DebugLoc &DL, const CalleeSavedInfo &CS) const {
    int FrameIdx = CS.getFrameIdx();
    int64_t Offset = MFI.getObjectOffset(FrameIdx);
    MCRegister Reg = CS.getReg();
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, RI.getDwarfRegNum(Reg, true), Offset));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }
};

class CFIRestoreRegisterEmitter {
  MachineFunction &MF;

public:
  CFIRestoreRegisterEmitter(MachineFunction &MF) : MF{MF} {};

  void emit(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
            const VCSIRRegisterInfo &RI, const VCSIRInstrInfo &TII,
            const DebugLoc &DL, const CalleeSavedInfo &CS) const {
    MCRegister Reg = CS.getReg();
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createRestore(nullptr, RI.getDwarfRegNum(Reg, true)));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameDestroy);
  }
};

} // namespace

template <typename Emitter>
void VCSIRFrameLowering::emitCFIForCSI(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const SmallVector<CalleeSavedInfo, 8> &CSI) const {
  MachineFunction *MF = MBB.getParent();
  const VCSIRRegisterInfo *RI = STI.getRegisterInfo();
  const VCSIRInstrInfo *TII = STI.getInstrInfo();
  DebugLoc DL = MBB.findDebugLoc(MBBI);

  Emitter E{*MF};
  for (const auto &CS : CSI)
    E.emit(MBB, MBBI, *RI, *TII, DL, CS);
}

static Align getABIStackAlignment() { return Align(4); }

VCSIRFrameLowering::VCSIRFrameLowering(const VCSIRSubtarget &STI)
    : TargetFrameLowering(StackGrowsDown, getABIStackAlignment(),
                          /*LocalAreaOffset=*/0,
                          /*TransientStackAlignment=*/getABIStackAlignment()),
      STI(STI) {}

// The register used to hold the frame pointer.
static constexpr MCPhysReg FPReg = VCSIR::X8;

// The register used to hold the stack pointer.
static constexpr MCPhysReg SPReg = VCSIR::X2;

// The register used to hold the return address.
static constexpr MCPhysReg RAReg = VCSIR::X1;

// LIst of CSRs that are given a fixed location by save/restore libcalls or
// Zcmp/Xqccmp Push/Pop. The order in this table indicates the order the
// registers are saved on the stack. Zcmp uses the reverse order of save/restore
// and Xqccmp on the stack, but this is handled when offsets are calculated.
static const MCPhysReg FixedCSRFIMap[] = {
    /*ra*/ RAReg,      /*s0*/ FPReg,      /*s1*/ VCSIR::X9,
    /*s2*/ VCSIR::X18, /*s3*/ VCSIR::X19, /*s4*/ VCSIR::X20,
    /*s5*/ VCSIR::X21, /*s6*/ VCSIR::X22, /*s7*/ VCSIR::X23,
    /*s8*/ VCSIR::X24, /*s9*/ VCSIR::X25, /*s10*/ VCSIR::X26,
    /*s11*/ VCSIR::X27};

// For now we use x3, a.k.a gp, as pointer to shadow call stack.
// User should not use x3 in their asm.
static void emitSCSPrologue(MachineFunction &MF, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            const DebugLoc &DL) {
  return;
}

static void emitSCSEpilogue(MachineFunction &MF, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            const DebugLoc &DL) {
  return;
}

// Get the ID of the libcall used for spilling and restoring callee saved
// registers. The ID is representative of the number of registers saved or
// restored by the libcall, except it is zero-indexed - ID 0 corresponds to a
// single register.
static int getLibCallID(const MachineFunction &MF,
                        const std::vector<CalleeSavedInfo> &CSI) {
  const auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();

  if (CSI.empty() || !RVFI->useSaveRestoreLibCalls(MF))
    return -1;

  MCRegister MaxReg;
  for (auto &CS : CSI)
    // assignCalleeSavedSpillSlots assigns negative frame indexes to
    // registers which can be saved by libcall.
    if (CS.getFrameIdx() < 0)
      MaxReg = std::max(MaxReg.id(), CS.getReg().id());

  if (!MaxReg)
    return -1;

  switch (MaxReg.id()) {
  default:
    llvm_unreachable("Something has gone wrong!");
    // clang-format off
  case /*s11*/ VCSIR::X27: return 12;
  case /*s10*/ VCSIR::X26: return 11;
  case /*s9*/  VCSIR::X25: return 10;
  case /*s8*/  VCSIR::X24: return 9;
  case /*s7*/  VCSIR::X23: return 8;
  case /*s6*/  VCSIR::X22: return 7;
  case /*s5*/  VCSIR::X21: return 6;
  case /*s4*/  VCSIR::X20: return 5;
  case /*s3*/  VCSIR::X19: return 4;
  case /*s2*/  VCSIR::X18: return 3;
  case /*s1*/  VCSIR::X9:  return 2;
  case /*s0*/  FPReg:  return 1;
  case /*ra*/  RAReg:  return 0;
    // clang-format on
  }
}

// Get the name of the libcall used for spilling callee saved registers.
// If this function will not use save/restore libcalls, then return a nullptr.
static const char *
getSpillLibCallName(const MachineFunction &MF,
                    const std::vector<CalleeSavedInfo> &CSI) {
  static const char *const SpillLibCalls[] = {
      "__vcsir_save_0", "__vcsir_save_1", "__vcsir_save_2",  "__vcsir_save_3",
      "__vcsir_save_4", "__vcsir_save_5", "__vcsir_save_6",  "__vcsir_save_7",
      "__vcsir_save_8", "__vcsir_save_9", "__vcsir_save_10", "__vcsir_save_11",
      "__vcsir_save_12"};

  int LibCallID = getLibCallID(MF, CSI);
  if (LibCallID == -1)
    return nullptr;
  return SpillLibCalls[LibCallID];
}

// Get the name of the libcall used for restoring callee saved registers.
// If this function will not use save/restore libcalls, then return a nullptr.
static const char *
getRestoreLibCallName(const MachineFunction &MF,
                      const std::vector<CalleeSavedInfo> &CSI) {
  static const char *const RestoreLibCalls[] = {
      "__vcsir_restore_0", "__vcsir_restore_1",  "__vcsir_restore_2",
      "__vcsir_restore_3", "__vcsir_restore_4",  "__vcsir_restore_5",
      "__vcsir_restore_6", "__vcsir_restore_7",  "__vcsir_restore_8",
      "__vcsir_restore_9", "__vcsir_restore_10", "__vcsir_restore_11",
      "__vcsir_restore_12"};

  int LibCallID = getLibCallID(MF, CSI);
  if (LibCallID == -1)
    return nullptr;
  return RestoreLibCalls[LibCallID];
}

// Get the max reg of Push/Pop for restoring callee saved registers.
static Register getMaxPushPopReg(const std::vector<CalleeSavedInfo> &CSI) {
  MCRegister MaxPushPopReg;
  for (auto &CS : CSI) {
    if (llvm::find_if(FixedCSRFIMap, [&](MCPhysReg P) {
          return P == CS.getReg();
        }) != std::end(FixedCSRFIMap))
      MaxPushPopReg = std::max(MaxPushPopReg.id(), CS.getReg().id());
  }
  assert(MaxPushPopReg != VCSIR::X26 && "x26 requires x27 to also be pushed");
  return MaxPushPopReg;
}

// Return true if the specified function should have a dedicated frame
// pointer register.  This is true if frame pointer elimination is
// disabled, if it needs dynamic stack realignment, if the function has
// variable sized allocas, or if the frame address is taken.
bool VCSIRFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         RegInfo->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
         MFI.isFrameAddressTaken();
}

bool VCSIRFrameLowering::hasBP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  // If we do not reserve stack space for outgoing arguments in prologue,
  // we will adjust the stack pointer before call instruction. After the
  // adjustment, we can not use SP to access the stack objects for the
  // arguments. Instead, use BP to access these stack objects.
  return (MFI.hasVarSizedObjects() ||
          (!hasReservedCallFrame(MF) && (!MFI.isMaxCallFrameSizeComputed() ||
                                         MFI.getMaxCallFrameSize() != 0))) &&
         TRI->hasStackRealignment(MF);
}

// Determines the size of the frame and maximum call frame size.
void VCSIRFrameLowering::determineFrameLayout(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t FrameSize = MFI.getStackSize();

  // Get the alignment.
  Align StackAlign = getStackAlign();

  // Make sure the frame is aligned.
  FrameSize = alignTo(FrameSize, StackAlign);

  // Update frame info.
  MFI.setStackSize(FrameSize);

  // When using SP or BP to access stack objects, we may require extra padding
  // to ensure the bottom of the RVV stack is correctly aligned within the main
  // stack. We calculate this as the amount required to align the scalar local
  // variable section up to the RVV alignment.
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  if (RVFI->getRVVStackSize() && (!hasFP(MF) || TRI->hasStackRealignment(MF))) {
    int ScalarLocalVarSize = FrameSize - RVFI->getCalleeSavedStackSize() -
                             RVFI->getVarArgsSaveSize();
    if (auto RVVPadding =
            offsetToAlignment(ScalarLocalVarSize, RVFI->getRVVStackAlign()))
      RVFI->setRVVPadding(RVVPadding);
  }
}

// Returns the stack size including RVV padding (when required), rounded back
// up to the required stack alignment.
uint64_t VCSIRFrameLowering::getStackSizeWithRVVPadding(
    const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();
  return alignTo(MFI.getStackSize() + RVFI->getRVVPadding(), getStackAlign());
}

static SmallVector<CalleeSavedInfo, 8>
getUnmanagedCSI(const MachineFunction &MF,
                const std::vector<CalleeSavedInfo> &CSI) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<CalleeSavedInfo, 8> NonLibcallCSI;

  for (auto &CS : CSI) {
    int FI = CS.getFrameIdx();
    if (FI >= 0 && MFI.getStackID(FI) == TargetStackID::Default)
      NonLibcallCSI.push_back(CS);
  }

  return NonLibcallCSI;
}

static SmallVector<CalleeSavedInfo, 8>
getRVVCalleeSavedInfo(const MachineFunction &MF,
                      const std::vector<CalleeSavedInfo> &CSI) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<CalleeSavedInfo, 8> RVVCSI;

  for (auto &CS : CSI) {
    int FI = CS.getFrameIdx();
    if (FI >= 0 && MFI.getStackID(FI) == TargetStackID::ScalableVector)
      RVVCSI.push_back(CS);
  }

  return RVVCSI;
}

static SmallVector<CalleeSavedInfo, 8>
getPushOrLibCallsSavedInfo(const MachineFunction &MF,
                           const std::vector<CalleeSavedInfo> &CSI) {
  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();

  SmallVector<CalleeSavedInfo, 8> PushOrLibCallsCSI;
  if (!RVFI->useSaveRestoreLibCalls(MF) && !RVFI->isPushable(MF))
    return PushOrLibCallsCSI;

  for (const auto &CS : CSI) {
    const auto *FII = llvm::find_if(
        FixedCSRFIMap, [&](MCPhysReg P) { return P == CS.getReg(); });
    if (FII != std::end(FixedCSRFIMap))
      PushOrLibCallsCSI.push_back(CS);
  }

  return PushOrLibCallsCSI;
}

static MCCFIInstruction createDefCFAExpression(const TargetRegisterInfo &TRI,
                                               Register Reg,
                                               uint64_t FixedOffset,
                                               uint64_t ScalableOffset) {
  assert(ScalableOffset != 0 && "Did not need to adjust CFA for RVV");
  SmallString<64> Expr;
  std::string CommentBuffer;
  llvm::raw_string_ostream Comment(CommentBuffer);
  // Build up the expression (Reg + FixedOffset + ScalableOffset * VLENB).
  unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
  Expr.push_back((uint8_t)(dwarf::DW_OP_breg0 + DwarfReg));
  Expr.push_back(0);
  if (Reg == SPReg)
    Comment << "sp";
  else
    Comment << printReg(Reg, &TRI);

  SmallString<64> DefCfaExpr;
  uint8_t Buffer[16];
  DefCfaExpr.push_back(dwarf::DW_CFA_def_cfa_expression);
  DefCfaExpr.append(Buffer, Buffer + encodeULEB128(Expr.size(), Buffer));
  DefCfaExpr.append(Expr.str());

  return MCCFIInstruction::createEscape(nullptr, DefCfaExpr.str(), SMLoc(),
                                        Comment.str());
}

static MCCFIInstruction createDefCFAOffset(const TargetRegisterInfo &TRI,
                                           Register Reg, uint64_t FixedOffset,
                                           uint64_t ScalableOffset) {
  assert(ScalableOffset != 0 && "Did not need to adjust CFA for RVV");
  SmallString<64> Expr;
  std::string CommentBuffer;
  llvm::raw_string_ostream Comment(CommentBuffer);
  Comment << printReg(Reg, &TRI) << "  @ cfa";

  SmallString<64> DefCfaExpr;
  uint8_t Buffer[16];
  unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
  DefCfaExpr.push_back(dwarf::DW_CFA_expression);
  DefCfaExpr.append(Buffer, Buffer + encodeULEB128(DwarfReg, Buffer));
  DefCfaExpr.append(Buffer, Buffer + encodeULEB128(Expr.size(), Buffer));
  DefCfaExpr.append(Expr.str());

  return MCCFIInstruction::createEscape(nullptr, DefCfaExpr.str(), SMLoc(),
                                        Comment.str());
}

// Allocate stack space and probe it if necessary.
void VCSIRFrameLowering::allocateStack(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       MachineFunction &MF, uint64_t Offset,
                                       uint64_t RealStackSize, bool EmitCFI,
                                       bool NeedProbe, uint64_t ProbeSize,
                                       bool DynAllocation) const {
  DebugLoc DL;
  const VCSIRRegisterInfo *RI = STI.getRegisterInfo();
  const VCSIRInstrInfo *TII = STI.getInstrInfo();

  // Simply allocate the stack if it's not big enough to require a probe.
  if (!NeedProbe || Offset <= ProbeSize) {
    RI->adjustReg(MBB, MBBI, DL, SPReg, SPReg, StackOffset::getFixed(-Offset),
                  MachineInstr::FrameSetup, getStackAlign());

    if (EmitCFI) {
      // Emit ".cfi_def_cfa_offset RealStackSize"
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, RealStackSize));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    if (NeedProbe && DynAllocation) {
      // s[d|w] zero, 0(sp)
      BuildMI(MBB, MBBI, DL, TII->get(VCSIR::SW))
          .addReg(VCSIR::X0)
          .addReg(SPReg)
          .addImm(0)
          .setMIFlags(MachineInstr::FrameSetup);
    }

    return;
  }
  llvm_unreachable("Unexpectedly large stack");
}

void VCSIRFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();
  const VCSIRRegisterInfo *RI = STI.getRegisterInfo();
  const VCSIRInstrInfo *TII = STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();

  Register BPReg = VCSIR::X9;

  // Debug location must be unknown since the first debug location is
  // usedVCSIRFram to determine the end of the prologue.
  DebugLoc DL;

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  // Emit prologue for shadow call stack.
  emitSCSPrologue(MF, MBB, MBBI, DL);

  auto FirstFrameSetup = MBBI;

  // Skip past all callee-saved register spill instructions.
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup))
    ++MBBI;

  // Determine the correct frame layout
  determineFrameLayout(MF);

  const auto &CSI = MFI.getCalleeSavedInfo();

  // Skip to before the spills of scalar callee-saved registers
  // FIXME: assumes exactly one instruction is used to restore each
  // callee-saved register.
  MBBI = std::prev(MBBI, getRVVCalleeSavedInfo(MF, CSI).size() +
                             getUnmanagedCSI(MF, CSI).size());

  // If libcalls are used to spill and restore callee-saved registers, the frame
  // has two sections; the opaque section managed by the libcalls, and the
  // section managed by MachineFrameInfo which can also hold callee saved
  // registers in fixed stack slots, both of which have negative frame indices.
  // This gets even more complicated when incoming arguments are passed via the
  // stack, as these too have negative frame indices. An example is detailed
  // below:
  //
  //  | incoming arg | <- FI[-3]
  //  | libcallspill |
  //  | calleespill  | <- FI[-2]
  //  | calleespill  | <- FI[-1]
  //  | this_frame   | <- FI[0]
  //
  // For negative frame indices, the offset from the frame pointer will differ
  // depending on which of these groups the frame index applies to.
  // The following calculates the correct offset knowing the number of callee
  // saved registers spilt by the two methods.
  if (int LibCallRegs = getLibCallID(MF, MFI.getCalleeSavedInfo()) + 1) {
    // Calculate the size of the frame managed by the libcall. The stack
    // alignment of these libcalls should be the same as how we set it in
    // getABIStackAlignment.
    unsigned LibCallFrameSize =
        alignTo((STI.getXLen() / 8) * LibCallRegs, getStackAlign());
    RVFI->setLibCallStackSize(LibCallFrameSize);

    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, LibCallFrameSize));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);

    emitCFIForCSI<CFISaveRegisterEmitter>(MBB, MBBI,
                                          getPushOrLibCallsSavedInfo(MF, CSI));
  }

  // FIXME (note copied from Lanai): This appears to be overallocating.  Needs
  // investigation. Get the number of bytes to allocate from the FrameInfo.
  uint64_t RealStackSize = getStackSizeWithRVVPadding(MF);
  uint64_t StackSize = RealStackSize - RVFI->getReservedSpillsSize();
  uint64_t RVVStackSize = RVFI->getRVVStackSize();

  // Early exit if there is no need to allocate on the stack
  if (RealStackSize == 0 && !MFI.adjustsStack() && RVVStackSize == 0)
    return;

  // If the stack pointer has been marked as reserved, then produce an error if
  // the frame requires stack allocation
  if (STI.isRegisterReservedByUser(SPReg))
    MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
        MF.getFunction(), "Stack pointer required, but has been reserved."});

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);
  // Split the SP adjustment to reduce the offsets of callee saved spill.
  if (FirstSPAdjustAmount) {
    StackSize = FirstSPAdjustAmount;
    RealStackSize = FirstSPAdjustAmount;
  }

  // Allocate space on the stack if necessary.
  auto &Subtarget = MF.getSubtarget<VCSIRSubtarget>();
  const VCSIRTargetLowering *TLI = Subtarget.getTargetLowering();
  bool NeedProbe = TLI->hasInlineStackProbe(MF);
  uint64_t ProbeSize = TLI->getStackProbeSize(MF, getStackAlign());
  bool DynAllocation =
      MF.getInfo<VCSIRMachineFunctionInfo>()->hasDynamicAllocation();
  if (StackSize != 0)
    allocateStack(MBB, MBBI, MF, StackSize, RealStackSize, /*EmitCFI=*/true,
                  NeedProbe, ProbeSize, DynAllocation);

  // The frame pointer is callee-saved, and code has been generated for us to
  // save it to the stack. We need to skip over the storing of callee-saved
  // registers as the frame pointer must be modified after it has been saved
  // to the stack, not before.
  // FIXME: assumes exactly one instruction is used to save each callee-saved
  // register.
  std::advance(MBBI, getUnmanagedCSI(MF, CSI).size());

  // Iterate over list of callee-saved registers and emit .cfi_offset
  // directives.
  emitCFIForCSI<CFISaveRegisterEmitter>(MBB, MBBI, getUnmanagedCSI(MF, CSI));

  // Generate new FP.
  if (hasFP(MF)) {
    if (STI.isRegisterReservedByUser(FPReg))
      MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
          MF.getFunction(), "Frame pointer required, but has been reserved."});
    // The frame pointer does need to be reserved from register allocation.
    assert(MF.getRegInfo().isReserved(FPReg) && "FP not reserved");

    RI->adjustReg(
        MBB, MBBI, DL, FPReg, SPReg,
        StackOffset::getFixed(RealStackSize - RVFI->getVarArgsSaveSize()),
        MachineInstr::FrameSetup, getStackAlign());

    // Emit ".cfi_def_cfa $fp, RVFI->getVarArgsSaveSize()"
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
        nullptr, RI->getDwarfRegNum(FPReg, true), RVFI->getVarArgsSaveSize()));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  uint64_t SecondSPAdjustAmount = 0;
  // Emit the second SP adjustment after saving callee saved registers.
  if (FirstSPAdjustAmount) {
    SecondSPAdjustAmount = getStackSizeWithRVVPadding(MF) - FirstSPAdjustAmount;
    assert(SecondSPAdjustAmount > 0 &&
           "SecondSPAdjustAmount should be greater than zero");

    allocateStack(MBB, MBBI, MF, SecondSPAdjustAmount,
                  getStackSizeWithRVVPadding(MF), !hasFP(MF), NeedProbe,
                  ProbeSize, DynAllocation);
  }

  if (hasFP(MF)) {
    // Realign Stack
    const VCSIRRegisterInfo *RI = STI.getRegisterInfo();
    if (RI->hasStackRealignment(MF)) {
      Align MaxAlignment = MFI.getMaxAlign();

      const VCSIRInstrInfo *TII = STI.getInstrInfo();
      if (isInt<12>(-(int)MaxAlignment.value())) {
        BuildMI(MBB, MBBI, DL, TII->get(VCSIR::ANDI), SPReg)
            .addReg(SPReg)
            .addImm(-(int)MaxAlignment.value())
            .setMIFlag(MachineInstr::FrameSetup);
      } else {
        unsigned ShiftAmount = Log2(MaxAlignment);
        Register VR =
            MF.getRegInfo().createVirtualRegister(&VCSIR::GPRRegClass);
        BuildMI(MBB, MBBI, DL, TII->get(VCSIR::SRLI), VR)
            .addReg(SPReg)
            .addImm(ShiftAmount)
            .setMIFlag(MachineInstr::FrameSetup);
        BuildMI(MBB, MBBI, DL, TII->get(VCSIR::SLLI), SPReg)
            .addReg(VR)
            .addImm(ShiftAmount)
            .setMIFlag(MachineInstr::FrameSetup);
      }
      if (NeedProbe && RVVStackSize == 0) {
        // Do a probe if the align + size allocated just passed the probe size
        // and was not yet probed.
        if (SecondSPAdjustAmount < ProbeSize &&
            SecondSPAdjustAmount + MaxAlignment.value() >= ProbeSize) {
          BuildMI(MBB, MBBI, DL, TII->get(VCSIR::SW))
              .addReg(VCSIR::X0)
              .addReg(SPReg)
              .addImm(0)
              .setMIFlags(MachineInstr::FrameSetup);
        }
      }
      // FP will be used to restore the frame in the epilogue, so we need
      // another base register BP to record SP after re-alignment. SP will
      // track the current stack after allocating variable sized objects.
      if (hasBP(MF)) {
        // move BP, SP
        BuildMI(MBB, MBBI, DL, TII->get(VCSIR::ADDI), BPReg)
            .addReg(SPReg)
            .addImm(0)
            .setMIFlag(MachineInstr::FrameSetup);
      }
    }
  }
}

void VCSIRFrameLowering::deallocateStack(MachineFunction &MF,
                                         MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MBBI,
                                         const DebugLoc &DL,
                                         uint64_t &StackSize,
                                         int64_t CFAOffset) const {
  const VCSIRRegisterInfo *RI = STI.getRegisterInfo();
  const VCSIRInstrInfo *TII = STI.getInstrInfo();

  RI->adjustReg(MBB, MBBI, DL, SPReg, SPReg, StackOffset::getFixed(StackSize),
                MachineInstr::FrameDestroy, getStackAlign());
  StackSize = 0;

  unsigned CFIIndex =
      MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, CFAOffset));
  BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlag(MachineInstr::FrameDestroy);
}

void VCSIRFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  const VCSIRRegisterInfo *RI = STI.getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();
  const VCSIRInstrInfo *TII = STI.getInstrInfo();

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  // Get the insert location for the epilogue. If there were no terminators in
  // the block, get the last instruction.
  MachineBasicBlock::iterator MBBI = MBB.end();
  DebugLoc DL;
  if (!MBB.empty()) {
    MBBI = MBB.getLastNonDebugInstr();
    if (MBBI != MBB.end())
      DL = MBBI->getDebugLoc();

    MBBI = MBB.getFirstTerminator();

    // Skip to before the restores of all callee-saved registers.
    while (MBBI != MBB.begin() &&
           std::prev(MBBI)->getFlag(MachineInstr::FrameDestroy))
      --MBBI;
  }

  const auto &CSI = MFI.getCalleeSavedInfo();

  // Skip to before the restores of scalar callee-saved registers
  // FIXME: assumes exactly one instruction is used to restore each
  // callee-saved register.
  auto FirstScalarCSRRestoreInsn =
      std::next(MBBI, getRVVCalleeSavedInfo(MF, CSI).size());

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);
  uint64_t RealStackSize = FirstSPAdjustAmount ? FirstSPAdjustAmount
                                               : getStackSizeWithRVVPadding(MF);
  uint64_t StackSize = FirstSPAdjustAmount ? FirstSPAdjustAmount
                                           : getStackSizeWithRVVPadding(MF) -
                                                 RVFI->getReservedSpillsSize();
  uint64_t FPOffset = RealStackSize - RVFI->getVarArgsSaveSize();
  uint64_t RVVStackSize = RVFI->getRVVStackSize();

  bool RestoreSPFromFP = RI->hasStackRealignment(MF) ||
                         MFI.hasVarSizedObjects() || !hasReservedCallFrame(MF);
  if (FirstSPAdjustAmount) {
    uint64_t SecondSPAdjustAmount =
        getStackSizeWithRVVPadding(MF) - FirstSPAdjustAmount;
    assert(SecondSPAdjustAmount > 0 &&
           "SecondSPAdjustAmount should be greater than zero");

    // If RestoreSPFromFP the stack pointer will be restored using the frame
    // pointer value.
    if (!RestoreSPFromFP)
      RI->adjustReg(MBB, FirstScalarCSRRestoreInsn, DL, SPReg, SPReg,
                    StackOffset::getFixed(SecondSPAdjustAmount),
                    MachineInstr::FrameDestroy, getStackAlign());

    if (!hasFP(MF)) {
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, FirstSPAdjustAmount));
      BuildMI(MBB, FirstScalarCSRRestoreInsn, DL,
              TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameDestroy);
    }
  }

  // Restore the stack pointer using the value of the frame pointer. Only
  // necessary if the stack pointer was modified, meaning the stack size is
  // unknown.
  //
  // In order to make sure the stack point is right through the EH region,
  // we also need to restore stack pointer from the frame pointer if we
  // don't preserve stack space within prologue/epilogue for outgoing variables,
  // normally it's just checking the variable sized object is present or not
  // is enough, but we also don't preserve that at prologue/epilogue when
  // have vector objects in stack.
  if (RestoreSPFromFP) {
    assert(hasFP(MF) && "frame pointer should not have been eliminated");
    RI->adjustReg(MBB, FirstScalarCSRRestoreInsn, DL, SPReg, FPReg,
                  StackOffset::getFixed(-FPOffset), MachineInstr::FrameDestroy,
                  getStackAlign());
  }

  if (hasFP(MF)) {
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
        nullptr, RI->getDwarfRegNum(SPReg, true), RealStackSize));
    BuildMI(MBB, FirstScalarCSRRestoreInsn, DL,
            TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  // Skip to after the restores of scalar callee-saved registers
  // FIXME: assumes exactly one instruction is used to restore each
  // callee-saved register.
  MBBI = std::next(FirstScalarCSRRestoreInsn, getUnmanagedCSI(MF, CSI).size());

  if (getLibCallID(MF, CSI) != -1) {
    // tail __vcsir_restore_[0-12] instruction is considered as a terminator,
    // therefore it is unnecessary to place any CFI instructions after it. Just
    // deallocate stack if needed and return.
    if (StackSize != 0)
      deallocateStack(MF, MBB, MBBI, DL, StackSize,
                      RVFI->getLibCallStackSize());

    // Emit epilogue for shadow call stack.
    emitSCSEpilogue(MF, MBB, MBBI, DL);
    return;
  }

  // Recover callee-saved registers.
  emitCFIForCSI<CFIRestoreRegisterEmitter>(MBB, MBBI, getUnmanagedCSI(MF, CSI));

  // Deallocate stack if StackSize isn't a zero yet
  if (StackSize != 0)
    deallocateStack(MF, MBB, MBBI, DL, StackSize, 0);

  // Emit epilogue for shadow call stack.
  emitSCSEpilogue(MF, MBB, MBBI, DL);
}

StackOffset
VCSIRFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                           Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();
  const auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();

  // Callee-saved registers should be referenced relative to the stack
  // pointer (positive offset), otherwise use the frame pointer (negative
  // offset).
  const auto &CSI = getUnmanagedCSI(MF, MFI.getCalleeSavedInfo());
  int MinCSFI = 0;
  int MaxCSFI = -1;
  StackOffset Offset;
  auto StackID = MFI.getStackID(FI);

  assert((StackID == TargetStackID::Default ||
          StackID == TargetStackID::ScalableVector) &&
         "Unexpected stack ID for the frame object.");
  if (StackID == TargetStackID::Default) {
    assert(getOffsetOfLocalArea() == 0 && "LocalAreaOffset is not 0!");
    Offset = StackOffset::getFixed(MFI.getObjectOffset(FI) +
                                   MFI.getOffsetAdjustment());
  } else if (StackID == TargetStackID::ScalableVector) {
    Offset = StackOffset::getScalable(MFI.getObjectOffset(FI));
  }

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);

  if (CSI.size()) {
    MinCSFI = CSI[0].getFrameIdx();
    MaxCSFI = CSI[CSI.size() - 1].getFrameIdx();
  }

  if (FI >= MinCSFI && FI <= MaxCSFI) {
    FrameReg = SPReg;

    if (FirstSPAdjustAmount)
      Offset += StackOffset::getFixed(FirstSPAdjustAmount);
    else
      Offset += StackOffset::getFixed(getStackSizeWithRVVPadding(MF));
    return Offset;
  }

  if (RI->hasStackRealignment(MF) && !MFI.isFixedObjectIndex(FI)) {
    // If the stack was realigned, the frame pointer is set in order to allow
    // SP to be restored, so we need another base register to record the stack
    // after realignment.
    // |--------------------------| -- <-- FP
    // | callee-allocated save    | | <----|
    // | area for register varargs| |      |
    // |--------------------------| |      |
    // | callee-saved registers   | |      |
    // |--------------------------| --     |
    // | realignment (the size of | |      |
    // | this area is not counted | |      |
    // | in MFI.getStackSize())   | |      |
    // |--------------------------| --     |-- MFI.getStackSize()
    // | RVV alignment padding    | |      |
    // | (not counted in          | |      |
    // | MFI.getStackSize() but   | |      |
    // | counted in               | |      |
    // | RVFI.getRVVStackSize())  | |      |
    // |--------------------------| --     |
    // | RVV objects              | |      |
    // | (not counted in          | |      |
    // | MFI.getStackSize())      | |      |
    // |--------------------------| --     |
    // | padding before RVV       | |      |
    // | (not counted in          | |      |
    // | MFI.getStackSize() or in | |      |
    // | RVFI.getRVVStackSize())  | |      |
    // |--------------------------| --     |
    // | scalar local variables   | | <----'
    // |--------------------------| -- <-- BP (if var sized objects present)
    // | VarSize objects          | |
    // |--------------------------| -- <-- SP
    if (hasBP(MF)) {
      FrameReg = VCSIR::X9;
    } else {
      // VarSize objects must be empty in this case!
      assert(!MFI.hasVarSizedObjects());
      FrameReg = SPReg;
    }
  } else {
    FrameReg = RI->getFrameRegister(MF);
  }

  if (FrameReg == FPReg) {
    Offset += StackOffset::getFixed(RVFI->getVarArgsSaveSize());
    // When using FP to access scalable vector objects, we need to minus
    // the frame size.
    //
    // |--------------------------| -- <-- FP
    // | callee-allocated save    | |
    // | area for register varargs| |
    // |--------------------------| |
    // | callee-saved registers   | |
    // |--------------------------| | MFI.getStackSize()
    // | scalar local variables   | |
    // |--------------------------| -- (Offset of RVV objects is from here.)
    // | RVV objects              |
    // |--------------------------|
    // | VarSize objects          |
    // |--------------------------| <-- SP
    if (MFI.getStackID(FI) == TargetStackID::ScalableVector) {
      assert(!RI->hasStackRealignment(MF) &&
             "Can't index across variable sized realign");
      // We don't expect any extra RVV alignment padding, as the stack size
      // and RVV object sections should be correct aligned in their own
      // right.
      assert(MFI.getStackSize() == getStackSizeWithRVVPadding(MF) &&
             "Inconsistent stack layout");
      Offset -= StackOffset::getFixed(MFI.getStackSize());
    }
    return Offset;
  }

  // This case handles indexing off both SP and BP.
  // If indexing off SP, there must not be any var sized objects
  assert(FrameReg == VCSIR::X9 || !MFI.hasVarSizedObjects());

  // When using SP to access frame objects, we need to add RVV stack size.
  //
  // |--------------------------| -- <-- FP
  // | callee-allocated save    | | <----|
  // | area for register varargs| |      |
  // |--------------------------| |      |
  // | callee-saved registers   | |      |
  // |--------------------------| --     |
  // | RVV alignment padding    | |      |
  // | (not counted in          | |      |
  // | MFI.getStackSize() but   | |      |
  // | counted in               | |      |
  // | RVFI.getRVVStackSize())  | |      |
  // |--------------------------| --     |
  // | RVV objects              | |      |-- MFI.getStackSize()
  // | (not counted in          | |      |
  // | MFI.getStackSize())      | |      |
  // |--------------------------| --     |
  // | padding before RVV       | |      |
  // | (not counted in          | |      |
  // | MFI.getStackSize())      | |      |
  // |--------------------------| --     |
  // | scalar local variables   | | <----'
  // |--------------------------| -- <-- BP (if var sized objects present)
  // | VarSize objects          | |
  // |--------------------------| -- <-- SP
  //
  // The total amount of padding surrounding RVV objects is described by
  // RVV->getRVVPadding() and it can be zero. It allows us to align the RVV
  // objects to the required alignment.
  if (MFI.getStackID(FI) == TargetStackID::Default) {
    if (MFI.isFixedObjectIndex(FI)) {
      assert(!RI->hasStackRealignment(MF) &&
             "Can't index across variable sized realign");
      Offset += StackOffset::get(getStackSizeWithRVVPadding(MF),
                                 RVFI->getRVVStackSize());
    } else {
      Offset += StackOffset::getFixed(MFI.getStackSize());
    }
  } else if (MFI.getStackID(FI) == TargetStackID::ScalableVector) {
    // Ensure the base of the RVV stack is correctly aligned: add on the
    // alignment padding.
    int ScalarLocalVarSize = MFI.getStackSize() -
                             RVFI->getCalleeSavedStackSize() -
                             RVFI->getVarArgsSaveSize() + RVFI->getRVVPadding();
    Offset += StackOffset::get(ScalarLocalVarSize, RVFI->getRVVStackSize());
  }
  return Offset;
}

void VCSIRFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  // Unconditionally spill RA and FP only if the function uses a frame
  // pointer.
  if (hasFP(MF)) {
    SavedRegs.set(RAReg);
    SavedRegs.set(FPReg);
  }
  // Mark BP as used if function has dedicated base pointer.
  if (hasBP(MF))
    SavedRegs.set(VCSIR::X9);

  // When using cm.push/pop we must save X27 if we save X26.
  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();
  if (RVFI->isPushable(MF) && SavedRegs.test(VCSIR::X26))
    SavedRegs.set(VCSIR::X27);
}

static unsigned estimateFunctionSizeInBytes(const MachineFunction &MF,
                                            const VCSIRInstrInfo &TII) {
  unsigned FnSize = 0;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      // Far branches over 20-bit offset will be relaxed in branch relaxation
      // pass. In the worst case, conditional branches will be relaxed into
      // the following instruction sequence. Unconditional branches are
      // relaxed in the same way, with the exception that there is no first
      // branch instruction.
      //
      //        foo
      //        bne     t5, t6, .rev_cond # `TII->getInstSizeInBytes(MI)` bytes
      //        sd      s11, 0(sp)        # 4 bytes, or 2 bytes in RVC
      //        jump    .restore, s11     # 8 bytes
      // .rev_cond
      //        bar
      //        j       .dest_bb          # 4 bytes, or 2 bytes in RVC
      // .restore:
      //        ld      s11, 0(sp)        # 4 bytes, or 2 bytes in RVC
      // .dest:
      //        baz
      if (MI.isConditionalBranch())
        FnSize += TII.getInstSizeInBytes(MI);
      if (MI.isConditionalBranch() || MI.isUnconditionalBranch()) {
        FnSize += 4 + 8 + 4 + 4;
        continue;
      }

      FnSize += TII.getInstSizeInBytes(MI);
    }
  }
  return FnSize;
}

void VCSIRFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  const VCSIRRegisterInfo *RegInfo =
      MF.getSubtarget<VCSIRSubtarget>().getRegisterInfo();
  const VCSIRInstrInfo *TII = MF.getSubtarget<VCSIRSubtarget>().getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterClass *RC = &VCSIR::GPRRegClass;
  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();

  int64_t RVVStackSize;
  Align RVVStackAlign;

  RVFI->setRVVStackSize(0);
  RVFI->setRVVStackAlign(Align(4));

  unsigned ScavSlotsNum = 0;

  // estimateStackSize has been observed to under-estimate the final stack
  // size, so give ourselves wiggle-room by checking for stack size
  // representable an 11-bit signed field rather than 12-bits.
  if (!isInt<11>(MFI.estimateStackSize(MF)))
    ScavSlotsNum = 1;

  // Far branches over 20-bit offset require a spill slot for scratch register.
  bool IsLargeFunction = !isInt<20>(estimateFunctionSizeInBytes(MF, *TII));
  if (IsLargeFunction)
    ScavSlotsNum = std::max(ScavSlotsNum, 1u);

  // RVV loads & stores have no capacity to hold the immediate address offsets
  // so we must always reserve an emergency spill slot if the MachineFunction
  // contains any RVV spills.
  ScavSlotsNum = ScavSlotsNum;

  for (unsigned I = 0; I < ScavSlotsNum; I++) {
    int FI = MFI.CreateSpillStackObject(RegInfo->getSpillSize(*RC),
                                        RegInfo->getSpillAlign(*RC));
    RS->addScavengingFrameIndex(FI);

    if (IsLargeFunction && RVFI->getBranchRelaxationScratchFrameIndex() == -1)
      RVFI->setBranchRelaxationScratchFrameIndex(FI);
  }

  unsigned Size = RVFI->getReservedSpillsSize();
  for (const auto &Info : MFI.getCalleeSavedInfo()) {
    int FrameIdx = Info.getFrameIdx();
    if (FrameIdx < 0 || MFI.getStackID(FrameIdx) != TargetStackID::Default)
      continue;

    Size += MFI.getObjectSize(FrameIdx);
  }
  RVFI->setCalleeSavedStackSize(Size);
}

bool VCSIRFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return true;
}

// Eliminate ADJCALLSTACKDOWN, ADJCALLSTACKUP pseudo instructions.
MachineBasicBlock::iterator VCSIRFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  DebugLoc DL = MI->getDebugLoc();

  if (!hasReservedCallFrame(MF)) {
    // If space has not been reserved for a call frame, ADJCALLSTACKDOWN and
    // ADJCALLSTACKUP must be converted to instructions manipulating the stack
    // pointer. This is necessary when there is a variable length stack
    // allocation (e.g. alloca), which means it's not possible to allocate
    // space for outgoing arguments from within the function prologue.
    int64_t Amount = MI->getOperand(0).getImm();

    if (Amount != 0) {
      // Ensure the stack remains aligned after adjustment.
      Amount = alignSPAdjust(Amount);

      if (MI->getOpcode() == VCSIR::ADJCALLSTACKDOWN)
        Amount = -Amount;

      const VCSIRRegisterInfo &RI = *STI.getRegisterInfo();
      RI.adjustReg(MBB, MI, DL, SPReg, SPReg, StackOffset::getFixed(Amount),
                   MachineInstr::NoFlags, getStackAlign());
    }
  }

  return MBB.erase(MI);
}

// We would like to split the SP adjustment to reduce prologue/epilogue
// as following instructions. In this way, the offset of the callee saved
// register could fit in a single store. Supposed that the first sp adjust
// amount is 2032.
//   add     sp,sp,-2032
//   sw      ra,2028(sp)
//   sw      s0,2024(sp)
//   sw      s1,2020(sp)
//   sw      s3,2012(sp)
//   sw      s4,2008(sp)
//   add     sp,sp,-64
uint64_t
VCSIRFrameLowering::getFirstSPAdjustAmount(const MachineFunction &MF) const {
  const auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  uint64_t StackSize = getStackSizeWithRVVPadding(MF);

  // Disable SplitSPAdjust if save-restore libcall is used. The callee-saved
  // registers will be pushed by the save-restore libcalls, so we don't have to
  // split the SP adjustment in this case.
  if (RVFI->getReservedSpillsSize())
    return 0;

  // Return the FirstSPAdjustAmount if the StackSize can not fit in a signed
  // 12-bit and there exists a callee-saved register needing to be pushed.
  if (!isInt<12>(StackSize) && (CSI.size() > 0)) {
    // FirstSPAdjustAmount is chosen at most as (2048 - StackAlign) because
    // 2048 will cause sp = sp + 2048 in the epilogue to be split into multiple
    // instructions. Offsets smaller than 2048 can fit in a single load/store
    // instruction, and we have to stick with the stack alignment. 2048 has
    // 16-byte alignment. The stack alignment for RV32 and RV64 is 16 and for
    // RV32E it is 4. So (2048 - StackAlign) will satisfy the stack alignment.
    const uint64_t StackAlign = getStackAlign().value();

    // Amount of (2048 - StackAlign) will prevent callee saved and restored
    // instructions be compressed, so try to adjust the amount to the largest
    // offset that stack compression instructions accept when target supports
    // compression instructions.
    return 2048 - StackAlign;
  }
  return 0;
}

bool VCSIRFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI, unsigned &MinCSFrameIndex,
    unsigned &MaxCSFrameIndex) const {
  // Early exit if no callee saved registers are modified!
  if (CSI.empty())
    return true;

  auto *RVFI = MF.getInfo<VCSIRMachineFunctionInfo>();

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();

  for (auto &CS : CSI) {
    MCRegister Reg = CS.getReg();
    const TargetRegisterClass *RC = RegInfo->getMinimalPhysRegClass(Reg);
    unsigned Size = RegInfo->getSpillSize(*RC);

    // This might need a fixed stack slot.
    if (RVFI->useSaveRestoreLibCalls(MF) || RVFI->isPushable(MF)) {
      const auto *FII = llvm::find_if(
          FixedCSRFIMap, [&](MCPhysReg P) { return P == CS.getReg(); });
      unsigned RegNum = std::distance(std::begin(FixedCSRFIMap), FII);

      if (FII != std::end(FixedCSRFIMap)) {
        int64_t Offset;
        if (RVFI->isPushable(MF))
          Offset = -int64_t(RVFI->getRVPushRegs() - RegNum) * Size;
        else
          Offset = -int64_t(RegNum + 1) * Size;

        int FrameIdx = MFI.CreateFixedSpillStackObject(Size, Offset);
        assert(FrameIdx < 0);
        CS.setFrameIdx(FrameIdx);
        continue;
      }
    }

    // Not a fixed slot.
    Align Alignment = RegInfo->getSpillAlign(*RC);
    // We may not be able to satisfy the desired alignment specification of
    // the TargetRegisterClass if the stack alignment is smaller. Use the
    // min.
    Alignment = std::min(Alignment, getStackAlign());
    int FrameIdx = MFI.CreateStackObject(Size, Alignment, true);
    if ((unsigned)FrameIdx < MinCSFrameIndex)
      MinCSFrameIndex = FrameIdx;
    if ((unsigned)FrameIdx > MaxCSFrameIndex)
      MaxCSFrameIndex = FrameIdx;
    CS.setFrameIdx(FrameIdx);
  }

  if (RVFI->isPushable(MF)) {
    // Allocate a fixed object that covers all the registers that are pushed.
    if (unsigned PushedRegs = RVFI->getRVPushRegs()) {
      int64_t PushedRegsBytes =
          static_cast<int64_t>(PushedRegs) * (STI.getXLen() / 8);
      MFI.CreateFixedSpillStackObject(PushedRegsBytes, -PushedRegsBytes);
    }
  } else if (int LibCallRegs = getLibCallID(MF, CSI) + 1) {
    // Allocate a fixed object that covers all of the stack allocated by the
    // libcall.
    int64_t LibCallFrameSize =
        alignTo((STI.getXLen() / 8) * LibCallRegs, getStackAlign());
    MFI.CreateFixedSpillStackObject(LibCallFrameSize, -LibCallFrameSize);
  }

  return true;
}

bool VCSIRFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL;
  if (MI != MBB.end() && !MI->isDebugInstr())
    DL = MI->getDebugLoc();

  // Emit CM.PUSH with base SPimm & evaluate Push stack
  VCSIRMachineFunctionInfo *RVFI = MF->getInfo<VCSIRMachineFunctionInfo>();
  if (const char *SpillLibCall = getSpillLibCallName(*MF, CSI)) {
    // Add spill libcall via non-callee-saved register t0.
    BuildMI(MBB, MI, DL, TII.get(VCSIR::PseudoCALLReg), VCSIR::X5)
        .addExternalSymbol(SpillLibCall, VCSIRII::MO_CALL)
        .setMIFlag(MachineInstr::FrameSetup);

    // Add registers spilled in libcall as liveins.
    for (auto &CS : CSI)
      MBB.addLiveIn(CS.getReg());
  }

  // Manually spill values not spilled by libcall & Push/Pop.
  const auto &UnmanagedCSI = getUnmanagedCSI(*MF, CSI);
  const auto &RVVCSI = getRVVCalleeSavedInfo(*MF, CSI);

  auto storeRegsToStackSlots = [&](decltype(UnmanagedCSI) CSInfo) {
    for (auto &CS : CSInfo) {
      // Insert the spill to the stack frame.
      MCRegister Reg = CS.getReg();
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.storeRegToStackSlot(MBB, MI, Reg, !MBB.isLiveIn(Reg),
                              CS.getFrameIdx(), RC, TRI, Register(),
                              MachineInstr::FrameSetup);
    }
  };
  storeRegsToStackSlots(UnmanagedCSI);
  storeRegsToStackSlots(RVVCSI);

  return true;
}

bool VCSIRFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL;
  if (MI != MBB.end() && !MI->isDebugInstr())
    DL = MI->getDebugLoc();

  // Manually restore values not restored by libcall & Push/Pop.
  // Reverse the restore order in epilog.  In addition, the return
  // address will be restored first in the epilogue. It increases
  // the opportunity to avoid the load-to-use data hazard between
  // loading RA and return by RA.  loadRegFromStackSlot can insert
  // multiple instructions.
  const auto &UnmanagedCSI = getUnmanagedCSI(*MF, CSI);
  const auto &RVVCSI = getRVVCalleeSavedInfo(*MF, CSI);

  auto loadRegFromStackSlot = [&](decltype(UnmanagedCSI) CSInfo) {
    for (auto &CS : CSInfo) {
      MCRegister Reg = CS.getReg();
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.loadRegFromStackSlot(MBB, MI, Reg, CS.getFrameIdx(), RC, TRI,
                               Register(), MachineInstr::FrameDestroy);
      assert(MI != MBB.begin() &&
             "loadRegFromStackSlot didn't insert any code!");
    }
  };
  loadRegFromStackSlot(RVVCSI);
  loadRegFromStackSlot(UnmanagedCSI);

  VCSIRMachineFunctionInfo *RVFI = MF->getInfo<VCSIRMachineFunctionInfo>();
  return true;
}

bool VCSIRFrameLowering::enableShrinkWrapping(const MachineFunction &MF) const {
  // Keep the conventional code flow when not optimizing.
  if (MF.getFunction().hasOptNone())
    return false;

  return true;
}

bool VCSIRFrameLowering::canUseAsPrologue(const MachineBasicBlock &MBB) const {
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);
  const MachineFunction *MF = MBB.getParent();
  const auto *RVFI = MF->getInfo<VCSIRMachineFunctionInfo>();

  if (!RVFI->useSaveRestoreLibCalls(*MF))
    return true;

  // Inserting a call to a __vcsir_save libcall requires the use of the register
  // t0 (X5) to hold the return address. Therefore if this register is already
  // used we can't insert the call.

  RegScavenger RS;
  RS.enterBasicBlock(*TmpMBB);
  return !RS.isRegUsed(VCSIR::X5);
}

bool VCSIRFrameLowering::canUseAsEpilogue(const MachineBasicBlock &MBB) const {
  const MachineFunction *MF = MBB.getParent();
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);
  const auto *RVFI = MF->getInfo<VCSIRMachineFunctionInfo>();

  if (!RVFI->useSaveRestoreLibCalls(*MF))
    return true;

  // Using the __vcsir_restore libcalls to restore CSRs requires a tail call.
  // This means if we still need to continue executing code within this function
  // the restore cannot take place in this basic block.

  if (MBB.succ_size() > 1)
    return false;

  MachineBasicBlock *SuccMBB =
      MBB.succ_empty() ? TmpMBB->getFallThrough() : *MBB.succ_begin();

  // Doing a tail call should be safe if there are no successors, because either
  // we have a returning block or the end of the block is unreachable, so the
  // restore will be eliminated regardless.
  if (!SuccMBB)
    return true;

  // The successor can only contain a return, since we would effectively be
  // replacing the successor with our own tail return at the end of our block.
  return SuccMBB->isReturnBlock() && SuccMBB->size() == 1;
}

bool VCSIRFrameLowering::isSupportedStackID(TargetStackID::Value ID) const {
  switch (ID) {
  case TargetStackID::Default:
  case TargetStackID::ScalableVector:
    return true;
  case TargetStackID::NoAlloc:
  case TargetStackID::SGPRSpill:
  case TargetStackID::WasmLocal:
    return false;
  }
  llvm_unreachable("Invalid TargetStackID::Value");
}

void VCSIRFrameLowering::inlineStackProbe(MachineFunction &MF,
                                          MachineBasicBlock &MBB) const {}
