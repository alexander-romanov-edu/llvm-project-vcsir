add_llvm_component_group(VCSIR)

set(LLVM_TARGET_DEFINITIONS VCSIR.td)
tablegen(LLVM VCSIRGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM VCSIRGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM VCSIRGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM VCSIRGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM VCSIRGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM VCSIRGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM VCSIRGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM VCSIRGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM VCSIRGenCallingConv.inc -gen-callingconv)
tablegen(LLVM VCSIRGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM VCSIRGenRegisterBank.inc -gen-register-bank)

set(LLVM_TARGET_DEFINITIONS VCSIRGISel.td)
tablegen(LLVM VCSIRGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM VCSIRGenO0PreLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="VCSIRO0PreLegalizerCombiner")
tablegen(LLVM VCSIRGenPreLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="VCSIRPreLegalizerCombiner")
tablegen(LLVM VCSIRGenPostLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="VCSIRPostLegalizerCombiner")

#tablegen(LLVM VCSIRGenSearchableTables.inc -gen-searchable-tables)
add_public_tablegen_target(VCSIRCommonTableGen)

add_llvm_target(VCSIRCodeGen
  VCSIRTargetMachine.cpp
  VCSIRRegisterInfo.cpp
  VCSIRInstrInfo.cpp
  VCSIRSubtarget.cpp
  VCSIRAsmPrinter.cpp
  VCSIRISelDagToDag.cpp
  VCSIRFrameLowering.cpp
  VCSIRCallingConv.cpp
  VCSIRExpandPseudoInsts.cpp
  VCSIRMachineFunctionInfo.cpp
  VCSIRISelLowering.cpp
  VCSIRSelectionDAGInfo.cpp
  VCSIRTargetObjectFile.cpp
  VCSIRTargetMachine.cpp
  VCSIRTargetTransformInfo.cpp
  GISel/VCSIRCallLowering.cpp
  GISel/VCSIRInstructionSelector.cpp
  GISel/VCSIRLegalizerInfo.cpp
  GISel/VCSIRPostLegalizerCombiner.cpp
  GISel/VCSIRO0PreLegalizerCombiner.cpp
  GISel/VCSIRRegisterBankInfo.cpp
  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  IPO
  MC
  VCSIRDesc
  VCSIRInfo
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  ADD_TO_COMPONENT
  VCSIR
)

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
add_subdirectory(Disassembler)
