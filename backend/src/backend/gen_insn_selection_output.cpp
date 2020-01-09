#include "backend/gen_insn_selection.hpp"
#include "sys/intrusive_list.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace std;

namespace gbe
{
  static void outputGenReg(const GenRegister& reg, bool dst)
  {
    ostringstream regName;
    if (reg.file == GEN_IMMEDIATE_VALUE || reg.file == GEN_GENERAL_REGISTER_FILE) {
      if (reg.file == GEN_IMMEDIATE_VALUE) {
        switch (reg.type) {
          case GEN_TYPE_UD:
          case GEN_TYPE_UW:
          case GEN_TYPE_UB:
          case GEN_TYPE_HF_IMM:
            regName << hex << "0x" << reg.value.ud  << dec;
            break;
          case GEN_TYPE_D:
          case GEN_TYPE_W:
          case GEN_TYPE_B:
            regName << reg.value.d;
            break;
          case GEN_TYPE_V:
            regName << hex << "0x" << reg.value.ud << dec;
            break;
          case GEN_TYPE_UL:
            regName << hex << "0x" << reg.value.u64 << dec;
            break;
          case GEN_TYPE_L:
            regName << reg.value.i64;
            break;
          case GEN_TYPE_F:
            regName << reg.value.f;
            break;
        }
      } else {
        if (reg.negation)
          regName << "-";
        if (reg.absolute)
          regName << "(abs)";
        regName << "%" << reg.value.reg;
        if (reg.subphysical)
          regName << "." << reg.subnr + reg.nr * GEN_REG_SIZE;

        if (dst)
          regName << "<" << GenRegister::hstride_size(reg) << ">";
        else
          regName << "<" << GenRegister::vstride_size(reg) << "," << GenRegister::width_size(reg) << "," << GenRegister::hstride_size(reg) << ">";
      }

      regName << ":";
      switch (reg.type) {
        case GEN_TYPE_UD:
          regName << "UD";
          break;
        case GEN_TYPE_UW:
          regName << "UW";
          break;
        case GEN_TYPE_UB:
          regName << "UB";
          break;
        case GEN_TYPE_HF_IMM:
          regName << "HF";
          break;
        case GEN_TYPE_D:
          regName << "D ";
          break;
        case GEN_TYPE_W:
          regName << "W ";
          break;
        case GEN_TYPE_B:
          regName << "B ";
          break;
        case GEN_TYPE_V:
          regName << "V ";
          break;
        case GEN_TYPE_UL:
          regName << "UL";
          break;
        case GEN_TYPE_L:
          regName << "L ";
          break;
        case GEN_TYPE_F:
          regName << "F ";
          break;
      }
    } else if (reg.file == GEN_ARCHITECTURE_REGISTER_FILE) {
      if(reg.nr == GEN_ARF_NULL) {
        regName << "null";
      } else if((reg.nr & GEN_ARF_ACCUMULATOR) == GEN_ARF_ACCUMULATOR) {
        regName << "acc." << (reg.nr & 0xF);
      } else {
        regName << "arf." << reg.nr;
      }
    } else
      assert(!"should not reach here");

    cout << right << setw(dst ? 15 : 23) << regName.str();
  }

#define OP_NAME_LENGTH 512
  void outputSelectionInst(const SelectionInstruction &insn) {
    cout << "[" << insn.ID << "]\t";
    if (insn.isLabel()) {
      cout << "L" << insn.index << ":" << endl;
      return;
    }

    if (insn.state.predicate != GEN_PREDICATE_NONE) {
      if (insn.state.physicalFlag == 0)
        cout << "(" << insn.state.flagIndex << ")";
      else
        cout << "(" << insn.state.flag << "." << insn.state.subFlag << ")";
    }

    cout << "\t";
    ostringstream opName;
    switch (insn.opcode) {
      #define DECL_SELECTION_IR(OP, FAMILY) case SEL_OP_##OP: opName << #OP; break;
      #include "backend/gen_insn_selection.hxx"
      #undef DECL_SELECTION_IR
    }

    if (insn.opcode == SEL_OP_CMP || insn.opcode == SEL_OP_I64CMP) {
      switch (insn.extra.function) {
        case GEN_CONDITIONAL_LE:
          opName << ".le";
          break;
        case GEN_CONDITIONAL_L:
          opName << ".lt";
          break;
        case GEN_CONDITIONAL_GE:
          opName << (".ge");
          break;
        case GEN_CONDITIONAL_G:
          opName << (".gt");
          break;
        case GEN_CONDITIONAL_EQ:
          opName << (".eq");
          break;
        case GEN_CONDITIONAL_NEQ:
          opName << (".ne");
          break;
      }
    }

    if (insn.opcode == SEL_OP_MATH) {
      switch (insn.extra.function) {
        case GEN_MATH_FUNCTION_INV:
          opName << (".inv");
          break;
        case GEN_MATH_FUNCTION_LOG:
          opName << (".log");
          break;
        case GEN_MATH_FUNCTION_EXP:
          opName << (".exp");
          break;
        case GEN_MATH_FUNCTION_SQRT:
          opName << (".sqrt");
          break;
        case GEN_MATH_FUNCTION_RSQ:
          opName << (".rsq");
          break;
        case GEN_MATH_FUNCTION_SIN:
          opName << (".sin");
          break;
        case GEN_MATH_FUNCTION_COS:
          opName << (".cos");
          break;
        case GEN_MATH_FUNCTION_FDIV:
          opName << (".fdiv");
          break;
        case GEN_MATH_FUNCTION_POW:
          opName << (".pow");
          break;
        case GEN_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER:
          opName << (".divmod");
          break;
        case GEN_MATH_FUNCTION_INT_DIV_QUOTIENT:
          opName << (".idiv");
          break;
        case GEN_MATH_FUNCTION_INT_DIV_REMAINDER:
          opName << (".mod");
          break;
      }
    }

    opName << "(" << insn.state.execWidth << ")";
    if(insn.opcode == SEL_OP_CMP || insn.opcode == SEL_OP_I64CMP || insn.state.modFlag) {
      if(!insn.state.physicalFlag) {
        opName << "[" << insn.state.flagIndex << "]";
      } else if(insn.opcode == SEL_OP_I64CMP || insn.opcode == SEL_OP_CMP) {
        opName << "[" << insn.state.flag << "." << insn.state.subFlag << "]";
      }
    }

    if(opName.tellp() >= 24) {
      cout << "opname too long: " << opName.str() << endl;
      return;
    }

    cout << left << setw(24) << opName.str();

    for (int i = 0; i < insn.dstNum; ++i)
    {
      auto dst = insn.dst(i);
      outputGenReg(dst, true);
      cout << " ";
    }

    if(insn.dstNum == 0) {
      cout << "                ";
    }

    cout << "= ";

    for (int i = 0; i < insn.srcNum; ++i)
    {
      GenRegister src = insn.src(i);
      outputGenReg(src, false);
      cout << " ";
    }

    switch(insn.opcode) {
    case SEL_OP_IF:
    case SEL_OP_BRC:
      cout << "; jip=L" << insn.index;
      cout << " uip=L" << insn.index1;
      break;
    case SEL_OP_ELSE:
    case SEL_OP_ENDIF:
    case SEL_OP_BRD:
    case SEL_OP_WHILE:
    case SEL_OP_JMPI:
      cout << "; jip=L" << insn.index;
      break;
    default:break;
    }

    cout << endl;
  }

  void outputSelectionIR(GenContext &ctx, Selection* sel, const char* KernelName)
  {
    cout << KernelName <<"'s SELECTION IR begin:" << endl;
    cout << "WARNING: not completed yet, welcome for the FIX!" << endl;
    for (const SelectionBlock &block : *sel->blockList) {
      for (const SelectionInstruction &insn : block.insnList) {
        outputSelectionInst(insn);
      }
      cout << endl;
    }
    cout << KernelName << "'s SELECTION IR end." << endl << endl;
  }

}
