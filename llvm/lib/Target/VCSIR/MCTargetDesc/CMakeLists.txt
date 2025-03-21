add_llvm_component_library(LLVMVCSIRDesc
  VCSIRTargetDesc.cpp
  VCSIRMCAsmInfo.cpp
  LINK_COMPONENTS
  MC
  Support
  VCSIRInfo

  ADD_TO_COMPONENT
  VCSIR
)
