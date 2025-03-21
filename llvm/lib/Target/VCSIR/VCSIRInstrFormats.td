//===-- VCSIRInstrFormats.td - VCSIR Instruction Formats --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class InstFormat<bits<3> val> { bits<3> Value = val; }

def InstFormatPseudo : InstFormat<0>;
def InstFormatR : InstFormat<1>;
def InstFormatI : InstFormat<2>;
def InstFormatS : InstFormat<3>;
def InstFormatB : InstFormat<4>;
def InstFormatU : InstFormat<5>;
def InstFormatJ : InstFormat<6>;

// The following opcode names match those given in Table 19.1 in the
// RISC-V User-level ISA specification ("RISC-V base opcode map").
class VCSIROpcode<string name, bits<7> val> {
  string Name = name;
  bits<7> Value = val;
}

def OPC_LOAD : VCSIROpcode<"LOAD", 0b0000011>;
def OPC_LOAD_FP : VCSIROpcode<"LOAD_FP", 0b0000111>;
def OPC_CUSTOM_0 : VCSIROpcode<"CUSTOM_0", 0b0001011>;
def OPC_MISC_MEM : VCSIROpcode<"MISC_MEM", 0b0001111>;
def OPC_OP_IMM : VCSIROpcode<"OP_IMM", 0b0010011>;
def OPC_AUIPC : VCSIROpcode<"AUIPC", 0b0010111>;
def OPC_OP_IMM_32 : VCSIROpcode<"OP_IMM_32", 0b0011011>;
def OPC_STORE : VCSIROpcode<"STORE", 0b0100011>;
def OPC_STORE_FP : VCSIROpcode<"STORE_FP", 0b0100111>;
def OPC_CUSTOM_1 : VCSIROpcode<"CUSTOM_1", 0b0101011>;
def OPC_AMO : VCSIROpcode<"AMO", 0b0101111>;
def OPC_OP : VCSIROpcode<"OP", 0b0110011>;
def OPC_LUI : VCSIROpcode<"LUI", 0b0110111>;
def OPC_OP_32 : VCSIROpcode<"OP_32", 0b0111011>;
def OPC_MADD : VCSIROpcode<"MADD", 0b1000011>;
def OPC_MSUB : VCSIROpcode<"MSUB", 0b1000111>;
def OPC_NMSUB : VCSIROpcode<"NMSUB", 0b1001011>;
def OPC_NMADD : VCSIROpcode<"NMADD", 0b1001111>;
def OPC_OP_FP : VCSIROpcode<"OP_FP", 0b1010011>;
def OPC_OP_V : VCSIROpcode<"OP_V", 0b1010111>;
def OPC_CUSTOM_2 : VCSIROpcode<"CUSTOM_2", 0b1011011>;
def OPC_BRANCH : VCSIROpcode<"BRANCH", 0b1100011>;
def OPC_JALR : VCSIROpcode<"JALR", 0b1100111>;
def OPC_JAL : VCSIROpcode<"JAL", 0b1101111>;
def OPC_SYSTEM : VCSIROpcode<"SYSTEM", 0b1110011>;
def OPC_OP_VE : VCSIROpcode<"OP_VE", 0b1110111>;
def OPC_CUSTOM_3 : VCSIROpcode<"CUSTOM_3", 0b1111011>;

class VCSIRInstCommon<dag outs, dag ins, string opcodestr, string argstr,
                      list<dag> pattern, InstFormat format> : Instruction {
  let Namespace = "VCSIR";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = opcodestr#!if(!empty(argstr), "", "\t"#argstr);
  let Pattern = pattern;

  let TSFlags{2 -0} = format.Value;

  bit IsTiedPseudo = 0;
  let TSFlags{3} = IsTiedPseudo;

  // Indicates that the result can be considered sign extended from bit 31. Some
  // instructions with this flag aren't W instructions, but are either sign
  // extended from a smaller size, always outputs a small integer, or put zeros
  // in bits 63:31. Used by the SExtWRemoval pass.
  bit IsSignExtendingOpW = 0;
  let TSFlags{4} = IsSignExtendingOpW;
}

class VCSIRInst<dag outs, dag ins, string opcodestr, string argstr,
                list<dag> pattern, InstFormat format>
    : VCSIRInstCommon<outs, ins, opcodestr, argstr, pattern, format> {
  field bits<32> Inst;
  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<32> SoftFail = 0;
  let Size = 4;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins, list<dag> pattern, string opcodestr = "",
             string argstr = "">
    : VCSIRInstCommon<outs, ins, opcodestr, argstr, pattern, InstFormatPseudo> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let Size = 4;
}

// Pseudo load instructions.
class PseudoLoad<string opcodestr>
    : Pseudo<(outs GPR:$rd), (ins bare_symbol:$addr), [], opcodestr,
             "$rd, $addr"> {
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 0;
  let isCodeGenOnly = 0;
  let isAsmParserOnly = 1;
}

// Pseudo store instructions.
class PseudoStore<string opcodestr, RegisterClass rsty = GPR>
    : Pseudo<(outs GPR:$tmp), (ins rsty:$rs, bare_symbol:$addr), [], opcodestr,
             "$rs, $addr, $tmp"> {
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 1;
  let isCodeGenOnly = 0;
  let isAsmParserOnly = 1;
}

// Instruction formats are listed in the order they appear in the RISC-V
// instruction set manual (R, R4, I, S, B, U, J).

// Common base class for R format instructions. Bits {31-25} should be set by
// the subclasses.
class VCSIRInstRBase<bits<3> funct3, VCSIROpcode opcode, dag outs, dag ins,
                     string opcodestr, string argstr>
    : VCSIRInst<outs, ins, opcodestr, argstr, [], InstFormatR> {
  bits<5> rs2;
  bits<5> rs1;
  bits<5> rd;

  let Inst{11 -7} = rs2;
  let Inst{16 -12} = rs1;
  let Inst{19 -17} = funct3;
  let Inst{24 -20} = rd;
  let Inst{31 -25} = opcode.Value;
}

class VCSIRInstR<bits<7> funct7, bits<3> funct3, VCSIROpcode opcode, dag outs,
                 dag ins, string opcodestr, string argstr>
    : VCSIRInstRBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  let Inst{6 -0} = funct7;
}

// Common base class for I format instructions. Bits {31-20} should be set by
// the subclasses.
class VCSIRInstIBase<bits<3> funct3, VCSIROpcode opcode, dag outs, dag ins,
                     string opcodestr, string argstr>
    : VCSIRInst<outs, ins, opcodestr, argstr, [], InstFormatI> {
  bits<5> rs1;
  bits<5> rd;

  let Inst{16 -12} = rs1;
  let Inst{19 -17} = funct3;
  let Inst{24 -20} = rd;
  let Inst{31 -25} = opcode.Value;
}

class VCSIRInstI<bits<3> funct3, VCSIROpcode opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : VCSIRInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  bits<12> imm12;

  let Inst{11 -0} = imm12;
}

class VCSIRInstIShift<bits<5> imm11_7, bits<3> funct3, VCSIROpcode opcode,
                      dag outs, dag ins, string opcodestr, string argstr>
    : VCSIRInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  bits<6> shamt;

  let Inst{4 -0} = imm11_7;
  let Inst{5} = 0;
  let Inst{11 -6} = shamt;
}

class VCSIRInstIUnary<bits<12> imm12, bits<3> funct3, VCSIROpcode opcode,
                      dag outs, dag ins, string opcodestr, string argstr>
    : VCSIRInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  let Inst{11 -0} = imm12;
}

class VCSIRInstS<bits<3> funct3, VCSIROpcode opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : VCSIRInst<outs, ins, opcodestr, argstr, [], InstFormatS> {
  bits<12> imm12;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{6 -0} = imm12{11 -5};
  let Inst{11 -7} = rs2;
  let Inst{16 -12} = rs1;
  let Inst{19 -17} = funct3;
  let Inst{24 -20} = imm12{4 -0};
  let Inst{31 -25} = opcode.Value;
}

class VCSIRInstB<bits<3> funct3, VCSIROpcode opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : VCSIRInst<outs, ins, opcodestr, argstr, [], InstFormatB> {
  bits<12> imm12;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{0} = imm12{11};
  let Inst{6 -1} = imm12{9 -4};
  let Inst{11 -7} = rs2;
  let Inst{16 -12} = rs1;
  let Inst{19 -17} = funct3;
  let Inst{23 -20} = imm12{3 -0};
  let Inst{4} = imm12{10};
  let Inst{31 -25} = opcode.Value;
}

class VCSIRInstU<VCSIROpcode opcode, dag outs, dag ins, string opcodestr,
                 string argstr>
    : VCSIRInst<outs, ins, opcodestr, argstr, [], InstFormatU> {
  bits<20> imm20;
  bits<5> rd;

  let Inst{19 -0} = imm20;
  let Inst{24 -20} = rd;
  let Inst{31 -25} = opcode.Value;
}

class VCSIRInstJ<VCSIROpcode opcode, dag outs, dag ins, string opcodestr,
                 string argstr>
    : VCSIRInst<outs, ins, opcodestr, argstr, [], InstFormatJ> {
  bits<20> imm20;
  bits<5> rd;

  let Inst{0} = imm20{19};
  let Inst{10 -1} = imm20{9 -0};
  let Inst{11} = imm20{10};
  let Inst{19 -12} = imm20{18 -11};
  let Inst{24 -20} = rd;
  let Inst{31 -25} = opcode.Value;
}
