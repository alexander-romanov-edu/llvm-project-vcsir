//===-- VCSIRRegisterInfo.td - VCSIR Register defs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the VCSIR register files
//===----------------------------------------------------------------------===//

let Namespace =
    "VCSIR" in class VCSIRReg<bits<5> Enc, string n, list<string> alt = []>
    : Register<n> {
  let HWEncoding{4 -0} = Enc;
  let AltNames = alt;
}

foreach I = 0 -31 in { def X#I : VCSIRReg<I, "x"#I>; }

def GPR : RegisterClass<"VCSIR", [i32], 32, (add(sequence "X%u", 0, 31))>;
