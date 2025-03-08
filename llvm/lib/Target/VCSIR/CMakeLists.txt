add_llvm_component_group(VCSIR)

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)

add_llvm_target(VCSIRCodeGen
  VCSIRTargetMachine.cpp
  LINK_COMPONENTS
  CodeGen
  Core
  Support
  Target
  VCSIRInfo
  ADD_TO_COMPONENT
  VCSIR
)
