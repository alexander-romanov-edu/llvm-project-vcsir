add_llvm_component_library(LLVMVCSIRInfo
  VCSIRTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  VCSIR
)
