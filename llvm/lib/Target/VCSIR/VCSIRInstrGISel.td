//===-- VCSIRInstrGISel.td - VCSIR GISel target pseudos ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
// VCSIR GlobalISel target pseudo instruction definitions. This is kept
// separately from the other tablegen files for organizational purposes, but
// share the same infrastructure.
//
//===----------------------------------------------------------------------===//

class VCSIRGenericInstruction : GenericInstruction {
  let Namespace = "VCSIR";
}

// Pseudo equivalent to a VCSIRISD::SRAW.
def G_SRAW : VCSIRGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}
def : GINodeEquiv<G_SRAW, vcsir_sraw>;

// Pseudo equivalent to a VCSIRISD::SRLW.
def G_SRLW : VCSIRGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}
def : GINodeEquiv<G_SRLW, vcsir_srlw>;

// Pseudo equivalent to a VCSIRISD::SLLW.
def G_SLLW : VCSIRGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}
def : GINodeEquiv<G_SLLW, vcsir_sllw>;

// Pseudo equivalent to a VCSIRISD::DIVW.
def G_DIVW : VCSIRGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}
def : GINodeEquiv<G_DIVW, vcsir_divw>;

// Pseudo equivalent to a VCSIRISD::DIVUW.
def G_DIVUW : VCSIRGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}
def : GINodeEquiv<G_DIVUW, vcsir_divuw>;

// Pseudo equivalent to a VCSIRISD::REMUW.
def G_REMUW : VCSIRGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}
def : GINodeEquiv<G_REMUW, vcsir_remuw>;

