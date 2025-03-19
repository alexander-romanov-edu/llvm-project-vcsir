#ifndef LLVM_LIB_TARGET_SIM_SIMISELLOWERING_H
#define LLVM_LIB_TARGET_SIM_SIMISELLOWERING_H

#include "Sim.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class SimSubtarget;
class SimTargetMachine;

namespace SimISD {

enum NodeType : unsigned {
  // Start the numbering where the builtin ops and target ops leave off.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET,
  CALL,
  BR_CC,
};

} // namespace SimISD

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SIM_SIMISELLOWERING_H