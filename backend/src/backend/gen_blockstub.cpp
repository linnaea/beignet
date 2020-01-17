#include "backend/gen_context.hpp"
#include "ir/value.hpp"
#include "gen_defs.hpp"
#include <set>

namespace gbe {
  using namespace ir;
  void GenContext::makeStubBlocks() {
    std::set<uint32_t> labeledPos;
    uint32_t nextLabel = 0;
    for (auto &label : labelPos) {
      labeledPos.insert(label.second);
      nextLabel = std::max(nextLabel, label.first.value());
    }

    map<uint32_t, uint32_t> linearSpans;
    uint32_t start = 0, curr = 0, prev = 0, next = 0;
    linearSpans[0] = 0;
    linearSpans[INT32_MAX] = UINT32_MAX;
    uint16_t minMove = std::max((uint16_t)8, this->getIFENDIFFix());
    for(curr = 0; curr < p->store.size(); curr = next) {
      auto *insn = reinterpret_cast<GenNativeInstruction *>(&p->store[curr]);
      prev = curr - 1;
      next = curr + (insn->header.cmpt_control ? 1 : 2);
      switch(insn->header.opcode) {
      case GEN_OPCODE_BRD:
      case GEN_OPCODE_BRC:
      case GEN_OPCODE_HALT:
      case GEN_OPCODE_IF:
      case GEN_OPCODE_ELSE:
      case GEN_OPCODE_ENDIF:
      case GEN_OPCODE_DO:
      case GEN_OPCODE_WHILE:
      case GEN_OPCODE_BREAK:
      case GEN_OPCODE_CONTINUE:
      case GEN_OPCODE_CALL:
      case GEN_OPCODE_RET:
        if(prev > start && prev - start > minMove && !linearSpans.count(start)) {
          linearSpans[start] = prev;
        }
        start = next;
        break;
      default:
        if(labeledPos.count(curr)) {
          if(prev > start && prev - start > minMove && !linearSpans.count(start)) {
            linearSpans[start] = prev;
          }
          start = curr;
        }
        break;
      }
    }

    linearSpans[start] = prev = curr - 1;

    GenNativeInstruction nop{};
    nop.header.opcode = GEN_OPCODE_NOP;

    GenNativeInstruction jmpi{};
    jmpi.header.opcode = GEN_OPCODE_JMPI;
    p->push();
    p->curr = GenInstructionState(1);
    p->curr.noMask = 1;
    p->setHeader(&jmpi);
    p->setDst(&jmpi, GenRegister::ip());
    p->setSrc0(&jmpi, GenRegister::ip());
    p->setSrc1(&jmpi, GenRegister::immd(0));
    p->pop();

    vector<uint32_t> posMapStub(p->store.size(), UINT32_MAX);
    vector<uint32_t> posMapBody(p->store.size(), UINT32_MAX);
    map<LabelIndex, uint32_t> newLabelStub;
    map<LabelIndex, uint32_t> newLabelBody;
    vector<GenInstruction> newStoreStub;
    vector<GenInstruction> newStoreBody;
    vector<std::pair<LabelIndex, uint32_t>> newBranchStub;
    vector<std::pair<LabelIndex, uint32_t>> newBranchBody;

    auto currentSpan = linearSpans.begin();
    for(curr = 0; curr < p->store.size(); curr++) {
      while(currentSpan->second < curr) {
        currentSpan++;
      }

      if(curr < currentSpan->first) {
        posMapStub[curr] = newStoreStub.size();
        newStoreStub.push_back(p->store[curr]);
      } else if(curr == currentSpan->first) {
        newLabelBody[LabelIndex(++nextLabel)] = newStoreBody.size();
        newStoreBody.push_back(p->store[curr]);

        posMapStub[curr] = newStoreStub.size();
        newBranchStub.push_back(std::make_pair(LabelIndex(nextLabel), newStoreStub.size()));
        newStoreStub.push_back(jmpi.low);
        newStoreStub.push_back(jmpi.high);
        if(p->gen < 75) {
          newStoreStub.push_back(nop.low);
          newStoreStub.push_back(nop.high);
        }
        if(currentSpan->second != prev) {
          newLabelStub[LabelIndex(++nextLabel)] = newStoreStub.size();
        }
      } else if(curr == currentSpan->second) {
        posMapBody[curr] = newStoreBody.size();
        newStoreBody.push_back(p->store[curr]);
        if(currentSpan->second != prev) {
          newBranchBody.push_back(std::make_pair(LabelIndex(nextLabel), newStoreBody.size()));
          newStoreBody.push_back(jmpi.low);
          newStoreBody.push_back(jmpi.high);
          if(p->gen < 75) {
            newStoreBody.push_back(nop.low);
            newStoreBody.push_back(nop.high);
          }
        }
      } else {
        posMapBody[curr] = newStoreBody.size();
        newStoreBody.push_back(p->store[curr]);
      }
    }

    vector<DebugInfo> newStoreDbg(newStoreStub.size());
    for(curr = 0; curr < p->store.size(); curr++) {
      if(posMapStub[curr] == UINT32_MAX) {
        posMapStub[curr] = posMapBody[curr] + newStoreStub.size();
      }

      if(!p->storedbg.empty()) {
        newStoreDbg[posMapStub[curr]] = p->storedbg[curr];
      }
    }

    for(auto &pair : newLabelBody) {
      pair.second += newStoreStub.size();
    }

    for(auto &pair : newBranchBody) {
      pair.second += newStoreStub.size();
    }

    newStoreStub.insert(newStoreStub.end(), newStoreBody.begin(), newStoreBody.end());

    for(auto &pair : labelPos) {
      pair.second = posMapStub[pair.second];
    }

    for(auto &pair : branchPos2) {
      pair.second = posMapStub[pair.second];
    }

    for(auto &pair : branchPos3) {
      pair.second = posMapStub[pair.second];
    }

    for(auto &pair : newLabelStub) {
      labelPos[pair.first] = pair.second;
    }

    for(auto &pair : newLabelBody) {
      labelPos[pair.first] = pair.second;
    }

    for(auto &pair : newBranchStub) {
      branchPos2.push_back(pair);
    }

    for(auto &pair : newBranchBody) {
      branchPos2.push_back(pair);
    }

    p->store = newStoreStub;
  }
}
