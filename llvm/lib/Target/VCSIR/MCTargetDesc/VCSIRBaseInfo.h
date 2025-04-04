//===-- VCSIRBaseInfo.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VCSIR_MCTARGETDESC_PCLPUINFO_H
#define LLVM_LIB_TARGET_VCSIR_MCTARGETDESC_PCLPUINFO_H

#include "MCTargetDesc/VCSIRTargetDesc.h"
#include "llvm/MC/MCInstrDesc.h"

namespace llvm {
namespace VCSIRII {
// VCSIR Specific Machine Operand Flags
enum {
  MO_None = 0,
  MO_CALL = 1,
  MO_LO = 3,
  MO_HI = 4,
  MO_PCREL_LO = 5,
  MO_PCREL_HI = 6,
  MO_GOT_HI = 7,
  MO_TPREL_LO = 8,
  MO_TPREL_HI = 9,
  MO_TPREL_ADD = 10,
  MO_TLS_GOT_HI = 11,
  MO_TLS_GD_HI = 12,
  MO_TLSDESC_HI = 13,
  MO_TLSDESC_LOAD_LO = 14,
  MO_TLSDESC_ADD_LO = 15,
  MO_TLSDESC_CALL = 16,

  // Used to differentiate between target-specific "direct" flags and "bitmask"
  // flags. A machine operand can only have one "direct" flag, but can have
  // multiple "bitmask" flags.
  MO_DIRECT_FLAG_MASK = 31
};

enum {
  InstFormatPseudo = 0,
  InstFormatR = 1,
  InstFormatR4 = 2,
  InstFormatI = 3,
  InstFormatS = 4,
  InstFormatB = 5,
  InstFormatU = 6,
  InstFormatJ = 7,
  InstFormatCR = 8,
  InstFormatCI = 9,
  InstFormatCSS = 10,
  InstFormatCIW = 11,
  InstFormatCL = 12,
  InstFormatCS = 13,
  InstFormatCA = 14,
  InstFormatCB = 15,
  InstFormatCJ = 16,
  InstFormatCU = 17,
  InstFormatCLB = 18,
  InstFormatCLH = 19,
  InstFormatCSB = 20,
  InstFormatCSH = 21,
  InstFormatOther = 22,

  InstFormatMask = 31,
  InstFormatShift = 0,

};
// Helper functions to read TSFlags.
/// \returns the format of the instruction.
static inline unsigned getFormat(uint64_t TSFlags) {
  return (TSFlags & InstFormatMask) >> InstFormatShift;
}
} // namespace VCSIRII

namespace VCSIRZC {
enum RLISTENCODE {
  INVALID_RLIST,
};
}
namespace VCSIROp {

enum OperandType : unsigned {
  OPERAND_FIRST_VCSIR_IMM = MCOI::OPERAND_FIRST_TARGET,
  OPERAND_UIMM1 = OPERAND_FIRST_VCSIR_IMM,
  OPERAND_UIMM2,
  OPERAND_UIMM2_LSB0,
  OPERAND_UIMM3,
  OPERAND_UIMM4,
  OPERAND_UIMM5,
  OPERAND_UIMM5_NONZERO,
  OPERAND_UIMM5_GT3,
  OPERAND_UIMM5_LSB0,
  OPERAND_UIMM6,
  OPERAND_UIMM6_LSB0,
  OPERAND_UIMM7,
  OPERAND_UIMM7_LSB00,
  OPERAND_UIMM7_LSB000,
  OPERAND_UIMM8_LSB00,
  OPERAND_UIMM8,
  OPERAND_UIMM8_LSB000,
  OPERAND_UIMM8_GE32,
  OPERAND_UIMM9_LSB000,
  OPERAND_UIMM10,
  OPERAND_UIMM10_LSB00_NONZERO,
  OPERAND_UIMM11,
  OPERAND_UIMM12,
  OPERAND_UIMM16,
  OPERAND_UIMM20,
  OPERAND_UIMMLOG2XLEN,
  OPERAND_UIMMLOG2XLEN_NONZERO,
  OPERAND_UIMM32,
  OPERAND_UIMM48,
  OPERAND_UIMM64,
  OPERAND_ZERO,
  OPERAND_SIMM5,
  OPERAND_SIMM5_PLUS1,
  OPERAND_SIMM6,
  OPERAND_SIMM6_NONZERO,
  OPERAND_SIMM10_LSB0000_NONZERO,
  OPERAND_SIMM12,
  OPERAND_SIMM12_LSB00000,
  OPERAND_SIMM26,
  OPERAND_SIMM32,
  OPERAND_CLUI_IMM,
  OPERAND_VTYPEI10,
  OPERAND_VTYPEI11,
  OPERAND_RVKRNUM,
  OPERAND_RVKRNUM_0_7,
  OPERAND_RVKRNUM_1_10,
  OPERAND_RVKRNUM_2_14,
  OPERAND_SPIMM,
  // Condition code used by select and short forward branch pseudos.
  OPERAND_COND_CODE,
};

} // namespace VCSIROp

} // namespace llvm

#endif // LLVM_LIB_TARGET_VCSIR_MCTARGETDESC_PCLPUINFO_H
