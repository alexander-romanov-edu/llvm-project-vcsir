//===-- VCSIRCallingConv.td - Calling Conventions VCSIR ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the VCSIR architecture.
//
//===----------------------------------------------------------------------===//

// The VCSIR calling convention is handled with custom code in
// VCSIRISelLowering.cpp (CC_VCSIR).

def CSR_ILP32E_LP64E : CalleeSavedRegs<(add X1, X8, X9)>;

def CSR_ILP32_LP64
    : CalleeSavedRegs<(add CSR_ILP32E_LP64E, (sequence "X%u", 18, 27))>;

// Needed for implementation of VCSIRRegisterInfo::getNoPreservedMask()
def CSR_NoRegs : CalleeSavedRegs<(add)>;

def CSR_IPRA : CalleeSavedRegs<(add X1)>;

// Interrupt handler needs to save/restore all registers that are used,
// both Caller and Callee saved registers.
def CSR_Interrupt : CalleeSavedRegs<(add X1, (sequence "X%u", 5, 31))>;

