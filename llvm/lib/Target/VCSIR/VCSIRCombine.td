//=- VCSIRCombine.td - Define VCSIR Combine Rules -----------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

def VCSIRPreLegalizerCombiner: GICombiner<
  "VCSIRPreLegalizerCombinerImpl", [all_combines]> {
}

def VCSIRO0PreLegalizerCombiner: GICombiner<
  "VCSIRO0PreLegalizerCombinerImpl", [optnone_combines]> {
}

// Post-legalization combines which are primarily optimizations.
// TODO: Add more combines.
def VCSIRPostLegalizerCombiner
    : GICombiner<"VCSIRPostLegalizerCombinerImpl",
                 [sub_to_add, combines_for_extload, redundant_and,
                  identity_combines, shift_immed_chain,
                  commute_constant_to_rhs, simplify_neg_minmax]> {
}
