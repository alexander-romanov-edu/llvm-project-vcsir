#include "SimISelLowering.h"
#include "Sim.h"
#include "SimRegisterInfo.h"
#include "SimSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"

#define DEBUG_TYPE "Sim-lower"

using namespace llvm;

SimTargetLowering::SimTargetLowering(const TargetMachine &TM,
                                     const SimSubtarget &STI)
    : TargetLowering(TM), STI(STI) {
  SIM_DUMP_RED
  addRegisterClass(MVT::i32, &Sim::GPRRegClass);
}

const char *SimTargetLowering::getTargetNodeName(unsigned Opcode) const {
  SIM_DUMP_RED
  switch (Opcode) {
  case SimISD::CALL:
    return "SimISD::CALL";
  case SimISD::RET:
    return "SimISD::RET";
  }
  return nullptr;
}
