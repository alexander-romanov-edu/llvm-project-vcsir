#ifndef LLVM_LIB_TARGET_VCSIR_VCSIRREGISTERINFO_H
#define LLVM_LIB_TARGET_VCSIR_VCSIRREGISTERINFO_H
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/TargetParser/VCSIRTargetParser.h"

#define GET_REGINFO_HEADER
#include "VCSIRGenRegisterInfo.inc"

namespace llvm {

struct VCSIRRegisterInfo : public VCSIRGenRegisterInfo {
public:
  VCSIRRegisterInfo();

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS) const override;
  void adjustReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator II,
                 const DebugLoc &DL, Register DestReg, Register SrcReg,
                 StackOffset Offset, MachineInstr::MIFlag Flag,
                 MaybeAlign RequiredAlign) const;

  Register getFrameRegister(const MachineFunction &MF) const override;
  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_VCSIR_VCSIRREGISTERINFO_H
