#include "VCSIRRegisterInfo.h"

#include "MCTargetDesc/VCSIRMatInt.h"
#include "VCSIRFrameLowering.h"
#include "VCSIRSubtarget.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_REGINFO_TARGET_DESC
#include "VCSIRGenRegisterInfo.inc"

namespace llvm {
VCSIRRegisterInfo::VCSIRRegisterInfo() : VCSIRGenRegisterInfo(VCSIR::X1) {}

const MCPhysReg *
VCSIRRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_ILP32_LP64_SaveList;
}

Register VCSIRRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return VCSIR::X2;
}
BitVector VCSIRRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  return BitVector(getNumRegs());
}
bool VCSIRRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected non-zero SPAdj value");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL = MI.getDebugLoc();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  StackOffset Offset =
      getFrameLowering(MF)->getFrameIndexReference(MF, FrameIndex, FrameReg);
  Offset += StackOffset::getFixed(MI.getOperand(FIOperandNum + 1).getImm());

  if (!isInt<32>(Offset.getFixed())) {
    report_fatal_error(
        "Frame offsets outside of the signed 32-bit range not supported");
  }

  int64_t Val = Offset.getFixed();
  int64_t Lo12 = SignExtend64<12>(Val);
  unsigned Opc = MI.getOpcode();
  if (Opc == VCSIR::ADDI && !isInt<12>(Val)) {
    // We chose to emit the canonical immediate sequence rather than folding
    // the offset into the using add under the theory that doing so doesn't
    // save dynamic instruction count and some target may fuse the canonical
    // 32 bit immediate sequence.  We still need to clear the portion of the
    // offset encoded in the immediate.
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
  } else {
    // We can encode an add with 12 bit signed immediate in the immediate
    // operand of our user instruction.  As a result, the remaining
    // offset can by construction, at worst, a LUI and a ADD.
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Lo12);
    Offset =
        StackOffset::get((uint64_t)Val - (uint64_t)Lo12, Offset.getScalable());
  }

  if (Offset.getScalable() || Offset.getFixed()) {
    Register DestReg;
    if (MI.getOpcode() == VCSIR::ADDI)
      DestReg = MI.getOperand(0).getReg();
    else
      DestReg = MRI.createVirtualRegister(&VCSIR::GPRRegClass);
    adjustReg(*II->getParent(), II, DL, DestReg, FrameReg, Offset,
              MachineInstr::NoFlags, std::nullopt);
    MI.getOperand(FIOperandNum)
        .ChangeToRegister(DestReg, /*IsDef*/ false,
                          /*IsImp*/ false,
                          /*IsKill*/ true);
  } else {
    MI.getOperand(FIOperandNum)
        .ChangeToRegister(FrameReg, /*IsDef*/ false,
                          /*IsImp*/ false,
                          /*IsKill*/ false);
  }

  // If after materializing the adjustment, we have a pointless ADDI, remove it
  if (MI.getOpcode() == VCSIR::ADDI &&
      MI.getOperand(0).getReg() == MI.getOperand(1).getReg() &&
      MI.getOperand(2).getImm() == 0) {
    MI.eraseFromParent();
    return true;
  }

  return false;
}

void VCSIRRegisterInfo::adjustReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator II,
                                  const DebugLoc &DL, Register DestReg,
                                  Register SrcReg, StackOffset Offset,
                                  MachineInstr::MIFlag Flag,
                                  MaybeAlign RequiredAlign) const {

  if (DestReg == SrcReg && !Offset.getFixed() && !Offset.getScalable())
    return;

  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const VCSIRSubtarget &ST = MF.getSubtarget<VCSIRSubtarget>();
  const VCSIRInstrInfo *TII = ST.getInstrInfo();

  bool KillSrcReg = false;

  int64_t Val = Offset.getFixed();
  if (DestReg == SrcReg && Val == 0)
    return;

  const uint64_t Align = RequiredAlign.valueOrOne().value();

  if (isInt<12>(Val)) {
    BuildMI(MBB, II, DL, TII->get(VCSIR::ADDI), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrcReg))
        .addImm(Val)
        .setMIFlag(Flag);
    return;
  }

  // Try to split the offset across two ADDIs. We need to keep the intermediate
  // result aligned after each ADDI.  We need to determine the maximum value we
  // can put in each ADDI. In the negative direction, we can use -2048 which is
  // always sufficiently aligned. In the positive direction, we need to find the
  // largest 12-bit immediate that is aligned.  Exclude -4096 since it can be
  // created with LUI.
  assert(Align < 2048 && "Required alignment too large");
  int64_t MaxPosAdjStep = 2048 - Align;
  if (Val > -4096 && Val <= (2 * MaxPosAdjStep)) {
    int64_t FirstAdj = Val < 0 ? -2048 : MaxPosAdjStep;
    Val -= FirstAdj;
    BuildMI(MBB, II, DL, TII->get(VCSIR::ADDI), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrcReg))
        .addImm(FirstAdj)
        .setMIFlag(Flag);
    BuildMI(MBB, II, DL, TII->get(VCSIR::ADDI), DestReg)
        .addReg(DestReg, RegState::Kill)
        .addImm(Val)
        .setMIFlag(Flag);
    return;
  }

  unsigned Opc = VCSIR::ADD;
  if (Val < 0) {
    Val = -Val;
    Opc = VCSIR::SUB;
  }

  Register ScratchReg = MRI.createVirtualRegister(&VCSIR::GPRRegClass);
  TII->movImm(MBB, II, DL, ScratchReg, Val, Flag);
  BuildMI(MBB, II, DL, TII->get(Opc), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrcReg))
      .addReg(ScratchReg, RegState::Kill)
      .setMIFlag(Flag);
}

} // namespace llvm
