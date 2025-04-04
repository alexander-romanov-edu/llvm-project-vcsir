//===-- VCSIRSchedule.td - VCSIR Scheduling Definitions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/// Define scheduler resources associated with def operands.
def WriteIALU       : SchedWrite;    // 32 or 64-bit integer ALU operations
def WriteIALU32     : SchedWrite;    // 32-bit integer ALU operations on RV64I
def WriteShiftImm   : SchedWrite;    // 32 or 64-bit shift by immediate operations
def WriteShiftReg   : SchedWrite;    // 32 or 64-bit shift by immediate operations
def WriteIDiv       : SchedWrite;    // 32-bit or 64-bit divide
def WriteIRem       : SchedWrite;    // 32-bit or 64-bit remainder
def WriteIMul       : SchedWrite;    // 32-bit or 64-bit multiply
def WriteJmp        : SchedWrite;    // Jump
def WriteJal        : SchedWrite;    // Jump and link
def WriteJalr       : SchedWrite;    // Jump and link register
def WriteNop        : SchedWrite;
def WriteLDB        : SchedWrite;    // Load byte
def WriteLDH        : SchedWrite;    // Load half-word
def WriteLDW        : SchedWrite;    // Load word
def WriteLDD        : SchedWrite;    // Load double-word
def WriteCSR        : SchedWrite;    // CSR instructions
def WriteSTB        : SchedWrite;    // Store byte
def WriteSTH        : SchedWrite;    // Store half-word
def WriteSTW        : SchedWrite;    // Store word
def WriteSTD        : SchedWrite;    // Store double-word
def ReadJmp         : SchedRead;
def ReadJalr        : SchedRead;
def ReadCSR         : SchedRead;
def ReadMemBase     : SchedRead;
def ReadFMemBase    : SchedRead;
def ReadStoreData   : SchedRead;
def ReadFStoreData  : SchedRead;
def ReadIALU        : SchedRead;
def ReadShiftImm    : SchedRead;
def ReadShiftReg    : SchedRead;
def ReadIDiv        : SchedRead;
def ReadIRem        : SchedRead;
def ReadIMul        : SchedRead;
