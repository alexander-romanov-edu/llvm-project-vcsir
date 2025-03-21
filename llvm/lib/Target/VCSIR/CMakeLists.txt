add_llvm_component_group(VCSIR)

set(LLVM_TARGET_DEFINITIONS VCSIR.td)
tablegen(LLVM VCSIRGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM VCSIRGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM VCSIRGenSubtargetInfo.inc -gen-subtarget)
add_public_tablegen_target(VCSIRCommonTableGen)

add_llvm_target(VCSIRCodeGen
  VCSIRTargetMachine.cpp
  VCSIRRegisterInfo.cpp
  VCSIRInstrInfo.cpp
  VCSIRSubtarget.cpp
  LINK_COMPONENTS
  CodeGen
  Core
  Support
  Target
  VCSIRInfo
  ADD_TO_COMPONENT
  VCSIR
)

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
