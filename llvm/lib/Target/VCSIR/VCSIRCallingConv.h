//===-- VCSIRCallingConv.h - VCSIR Custom CC Routines ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the custom routines for the VCSIR Calling Convention.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/VCSIRBaseInfo.h"
#include "llvm/CodeGen/CallingConvLower.h"

namespace llvm {

/// VCSIRCCAssignFn - This target-specific function extends the default
/// CCValAssign with additional information used to lower VCSIR calling
/// conventions.
typedef bool VCSIRCCAssignFn(unsigned ValNo, MVT ValVT, MVT LocVT,
                             CCValAssign::LocInfo LocInfo,
                             ISD::ArgFlagsTy ArgFlags, CCState &State,
                             bool IsFixed, bool IsRet, Type *OrigTy);

bool CC_VCSIR(unsigned ValNo, MVT ValVT, MVT LocVT,
              CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
              CCState &State, bool IsFixed, bool IsRet, Type *OrigTy);

bool CC_VCSIR_FastCC(unsigned ValNo, MVT ValVT, MVT LocVT,
                     CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                     CCState &State, bool IsFixed, bool IsRet, Type *OrigTy);

bool CC_VCSIR_GHC(unsigned ValNo, MVT ValVT, MVT LocVT,
                  CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                  CCState &State);

namespace VCSIR {

ArrayRef<MCPhysReg> getArgGPRs();

} // end namespace VCSIR

} // end namespace llvm
