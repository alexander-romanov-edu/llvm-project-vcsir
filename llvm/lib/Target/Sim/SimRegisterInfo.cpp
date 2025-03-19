#include "SimRegisterInfo.h"
#include "Sim.h"
#include "SimFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "SimGenRegisterInfo.inc"

SimRegisterInfo::SimRegisterInfo() : SimGenRegisterInfo(Sim::R0) {
  SIM_DUMP_GREEN
}
const MCPhysReg *
SimRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  SIM_DUMP_GREEN
  return CSR_Sim_SaveList;
}

BitVector SimRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  SIM_DUMP_GREEN
  SimFrameLowering const *TFI = getFrameLowering(MF);

  BitVector Reserved(getNumRegs());
  Reserved.set(Sim::R1);

  if (TFI->hasFP(MF)) {
    Reserved.set(Sim::R2);
  }
  return Reserved;
}

bool SimRegisterInfo::requiresRegisterScavenging(
    const MachineFunction &MF) const {
  return false;
}

bool SimRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  SIM_DUMP_GREEN
  assert(SPAdj == 0 && "Unexpected non-zero SPAdj value");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  DebugLoc DL = MI.getDebugLoc();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  int Offset = getFrameLowering(MF)
                   ->getFrameIndexReference(MF, FrameIndex, FrameReg)
                   .getFixed();
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  if (!isInt<16>(Offset)) {
    llvm_unreachable("");
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false, false, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register SimRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  SIM_DUMP_GREEN
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? Sim::R2 : Sim::R1;
}

const uint32_t *
SimRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  SIM_DUMP_GREEN
  return CSR_Sim_RegMask;
}
