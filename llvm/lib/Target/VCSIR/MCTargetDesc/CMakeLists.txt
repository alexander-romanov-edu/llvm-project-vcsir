add_llvm_component_library(LLVMVCSIRDesc
  VCSIRTargetDesc.cpp

  LINK_COMPONENTS
  MC
  Support
  VCSIRInfo

  ADD_TO_COMPONENT
  VCSIR
)
