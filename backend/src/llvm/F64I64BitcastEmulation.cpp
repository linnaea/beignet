#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace gbe {
struct F64I64BitcastEmulationPass : public FunctionPass {
  static char ID;
  F64I64BitcastEmulationPass() : FunctionPass(ID) {}

  bool hasProblematicBitcast(BasicBlock &BB) {
    for(auto I = BB.begin(), IE = BB.end(); I != IE; ++I) {
      if(I->getOpcode() != Instruction::BitCast) continue;
      Type *srcType = I->getOperand(0)->getType();
      Type *dstType = I->getType();
      if(srcType->isDoubleTy() && dstType->isIntegerTy(64)) return true;
      if(dstType->isDoubleTy() && srcType->isIntegerTy(64)) return true;
    }
    return false;
  }

  bool hasProblematicBitcast(Function &F) {
    for(auto BB = F.begin(), BE = F.end(); BB != BE; ++BB) {
      if(hasProblematicBitcast(*BB)) return true;
    }
    return false;
  }

  bool replaceProblematicBitcast(BasicBlock &BB, Value *convF64, Value *convI64) {
    for(auto I = BB.begin(), IE = BB.end(); I != IE; ++I) {
      if(I->getOpcode() != Instruction::BitCast) continue;
      Type *srcType = I->getOperand(0)->getType();
      Type *dstType = I->getType();

      StoreInst *store; LoadInst *load;
      if(srcType->isDoubleTy() && dstType->isIntegerTy(64)) {
        store = new StoreInst(I->getOperand(0), convF64);
        load = new LoadInst(convI64);
      } else if(dstType->isDoubleTy() && srcType->isIntegerTy(64)) {
        store = new StoreInst(I->getOperand(0), convI64);
        load = new LoadInst(convF64);
      } else {
        continue;
      }
#if LLVM_VERSION_MAJOR >= 10
      load->setAlignment(Align(8));
      store->setAlignment(Align(8));
#else
      load->setAlignment(8);
      store->setAlignment(8);
#endif

      BasicBlock::iterator pos(I);
      BB.getInstList().insert(pos, store);
      ReplaceInstWithInst(BB.getInstList(), pos, load);
      return true;
    }
    return false;
  }

  void replaceProblematicBitcast(Function &F, Value *convF64, Value *convI64) {
    for(auto BB = F.begin(), BE = F.end(); BB != BE; ++BB) {
      while(replaceProblematicBitcast(*BB, convF64, convI64)) { }
    }
  }

  bool runOnFunction(Function &F) override {
    if(!hasProblematicBitcast(F)) {
      return false;
    }

    LLVMContext &ctx = F.getContext();
    AllocaInst *convF64 = new AllocaInst(Type::getDoubleTy(ctx), 0, "conv.f64");
#if LLVM_VERSION_MAJOR >= 10
    convF64->setAlignment(Align(8));
#else
    convF64->setAlignment(8);
#endif

    CastInst *convI64 = CastInst::CreatePointerCast(convF64, Type::getInt64Ty(ctx)->getPointerTo(), "conv.i64");
    BasicBlock &entryBlock = F.getEntryBlock();
    entryBlock.getInstList().insert(entryBlock.getFirstInsertionPt(), convI64);
    entryBlock.getInstList().insert(entryBlock.getFirstInsertionPt(), convF64);

    replaceProblematicBitcast(F, convF64, convI64);
    return true;
  }
};

char F64I64BitcastEmulationPass::ID = 0;
// static RegisterPass<F64I64BitcastEmulationPass> X("emu-f64i64-bitcast", "F64 <-> I64 Bitcast Emulation", false, false);

FunctionPass *createF64I64BitcastEmulationPass() {
  return new F64I64BitcastEmulationPass();
}
}
