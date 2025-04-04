add_llvm_component_library(LLVMVCSIRDisassembler
  VCSIRDisassembler.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  VCSIRDesc
  VCSIRInfo
  Support

  ADD_TO_COMPONENT
  VCSIR
  )
