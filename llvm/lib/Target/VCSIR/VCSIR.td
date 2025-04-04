//===-- VCSIR.td - Describe the VCSIR Target Machine -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"
include "VCSIRRegisterInfo.td"
include "VCSIRSchedule.td"
include "VCSIRCallingConv.td"
include "VCSIRInstrInfo.td"
include "GISel/VCSIRRegisterBanks.td"

def VCSIRAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 1;
}
def VCSIRAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}
class Proc<string Name, list<SubtargetFeature> Features>
 : Processor<Name, NoItineraries, Features>;
 
def : Proc<"generic", []>;

def VCSIRInstrInfo : InstrInfo;
def VCSIR : Target {
  let InstructionSet = VCSIRInstrInfo;
  let AssemblyWriters = [VCSIRAsmWriter];
}
