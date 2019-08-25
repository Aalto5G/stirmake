#ifndef _STIROPCODES_H_
#define _STIROPCODES_H_

enum stir_opcode {
  STIR_OPCODE_TOP_DIR = 64,
  STIR_OPCODE_CUR_DIR_FROM_TOP = 65,
  STIR_OPCODE_DEP_ADD = 66,
  STIR_OPCODE_RULE_ADD = 67,
  STIR_OPCODE_SUFSUBALL = 68,
  STIR_OPCODE_SUFSUBONE = 69,
  STIR_OPCODE_SUFFILTER = 70,
  STIR_OPCODE_PATHSUFFIX = 71,
  STIR_OPCODE_PATHBASENAME = 72,
  STIR_OPCODE_PATHDIR = 73,
  STIR_OPCODE_PATHNOTDIR = 74,
};

#endif
