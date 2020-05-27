/*
 * Copyright © 2012 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file llvm_intrinisc_lowering.cpp
 * \author Yang Rong <rong.r.yang@intel.com>
 */

#include "llvm_includes.hpp"

#include "llvm/llvm_gen_backend.hpp"
#include "sys/map.hpp"


using namespace llvm;

namespace gbe {
    class GenIntrinsicLowering : public GenBasicBlockPass
    {
    public:
      static char ID;
      GenIntrinsicLowering() :
        GenBasicBlockPass(ID) {}

      void getAnalysisUsage(AnalysisUsage &AU) const override {

      }

#if LLVM_VERSION_MAJOR * 10 + LLVM_VERSION_MINOR >= 40
      StringRef getPassName() const override
#else
      const char *getPassName() const override
#endif
      {
        return "SPIR backend: lowering intrinsics";
      }
      static char convertSpaceToName(Value *val) {
        const uint32_t space = val->getType()->getPointerAddressSpace();
        switch(space) {
          case 0:
            return 'p';
          case 1:
            return 'g';
          case 2:
            return 'c';
          case 3:
            return 'l';
          case 4:
            return 'n';
          default:
            GBE_ASSERTM(0, "Non support address space");
            return '\0';
        }
      }
      static CallInst *replaceCallWith(const char *NewFn, CallInst *CI,
                                     Value **ArgBegin, Value **ArgEnd,
                                     Type *RetTy)
      {
        // If we haven't already looked up this function, check to see if the
        // program already contains a function with this name.
        Module *M = CI->getParent()->getParent()->getParent();
        // Get or insert the definition now.
        std::vector<Type *> ParamTys;
        for (Value** I = ArgBegin; I != ArgEnd; ++I)
          ParamTys.push_back((*I)->getType());
#if LLVM_VERSION_MAJOR >= 9
        FunctionCallee FCache = M->getOrInsertFunction(NewFn,
#else
        Constant* FCache = M->getOrInsertFunction(NewFn,
#endif
                                        FunctionType::get(RetTy, ParamTys, false));

        IRBuilder<> Builder(CI->getParent(), BasicBlock::iterator(CI));
        SmallVector<Value *, 8> Args(ArgBegin, ArgEnd);
        CallInst *NewCI = Builder.CreateCall(FCache, Args);
        NewCI->setName(CI->getName());
        if (!CI->use_empty())
          CI->replaceAllUsesWith(NewCI);
        CI->eraseFromParent();
        return NewCI;
      }
      bool runOnBasicBlock(BasicBlock &BB) override
      {
        bool changedBlock = false;
        Module *M = BB.getParent()->getParent();

        DataLayout TD(M);
        LLVMContext &Context = BB.getContext();
        for (BasicBlock::iterator DI = BB.begin(); DI != BB.end(); ) {
          Instruction *Inst = &*DI++;
          auto* CI = dyn_cast<CallInst>(Inst);
          if(CI == nullptr)
            continue;

          IRBuilder<> Builder(&BB, BasicBlock::iterator(CI));
          // only support memcpy and memset
          if (Function *F = CI->getCalledFunction()) {
            const auto intrinsicID = (Intrinsic::ID) F->getIntrinsicID();
            if (intrinsicID == 0)
              continue;
            switch (intrinsicID) {
              case Intrinsic::memcpy: {
                Type *IntPtr = TD.getIntPtrType(Context);
                Value *Size = Builder.CreateIntCast(CI->getArgOperand(2), IntPtr,
                                                    /* isSigned */ false);
                Value *align = Builder.CreateIntCast(CI->getArgOperand(3), IntPtr,
                                                    /* isSigned */ false);
                auto *ci = dyn_cast<ConstantInt>(align);
                Value *Ops[3];
                Ops[0] = CI->getArgOperand(0);
                Ops[1] = CI->getArgOperand(1);
                Ops[2] = Size;
                char name[24] = "__gen_memcpy_xx";
                name[13] = convertSpaceToName(Ops[0]);
                name[14] = convertSpaceToName(Ops[1]);
                if(ci && (ci->getZExtValue() % 4 == 0)) //alignment is constant and 4 byte align
                  strcat(name, "_align");
                replaceCallWith(name, CI, Ops, Ops+3, Type::getVoidTy(Context));
                changedBlock = true;
                break;
              }
              case Intrinsic::memset: {
                Value *Op0 = CI->getArgOperand(0);
                Value *val = Builder.CreateIntCast(CI->getArgOperand(1), IntegerType::getInt8Ty(Context),
                                                    /* isSigned */ false);
                Type *IntPtr = TD.getIntPtrType(Op0->getType());
                Value *Size = Builder.CreateIntCast(CI->getArgOperand(2), IntPtr,
                                                    /* isSigned */ false);
                Value *align = Builder.CreateIntCast(CI->getArgOperand(3), IntPtr,
                                                    /* isSigned */ false);
                auto *ci = dyn_cast<ConstantInt>(align);
                Value *Ops[3];
                Ops[0] = Op0;
                // Extend the amount to i32.
                Ops[1] = val;
                Ops[2] = Size;
                char name[24] = "__gen_memset_x";
                name[13] = convertSpaceToName(Ops[0]);
                if(ci && (ci->getZExtValue() % 4 == 0)) //alignment is constant and 4 byte align
                  strcat(name, "_align");
                replaceCallWith(name, CI, Ops, Ops+3, Type::getVoidTy(Context));
                changedBlock = true;
                break;
              }
#if LLVM_VERSION_MAJOR >= 7
              case Intrinsic::fshl:
              case Intrinsic::fshr: {
                // %result = call T @llvm.fsh{l,r}.T(T %a, T %b, T %c)
                // ->
                // ;intrinsic has different semantic on shift amount larger than bit size
                // realShift = c % Tbits
                // funneled = Tbits - realShift
                // visibleBits = a {<<,>>} realShift
                // funnelBits = b {>>,<<} funneled
                // result = visibleBits | funnelBits
                //
                // %realShift = urem T Tbits, %c
                // %funShift = sub T Tbits, %realShift
                // %visBits = {shl,lshr} T %a, %realShift
                // %funBits = {lshr,shl} T %b, %funShift
                // %result = or T %visBits, %funBits
                auto operandBits = CI->getArgOperand(0)->getType()->getScalarSizeInBits();
                Constant *nBits = Builder.getIntN(operandBits, operandBits);
                if(CI->getType()->isVectorTy()) {
                  nBits = ConstantVector::getSplat(CI->getType()->getVectorNumElements(), nBits);
                }

                auto realShift = Builder.CreateURem(CI->getArgOperand(2), nBits);
                auto funShift = Builder.CreateSub(nBits, realShift);

                if(intrinsicID == Intrinsic::fshr) {
                  std::swap(realShift, funShift);
                }

                auto upperBits = Builder.CreateShl(CI->getArgOperand(0), realShift);
                auto lowerBits = Builder.CreateLShr(CI->getArgOperand(1), funShift);
                auto combined = Builder.CreateOr(upperBits, lowerBits);

                CI->replaceAllUsesWith(combined);
                CI->eraseFromParent();
                changedBlock = true;
                break;
              }
#endif
#if LLVM_VERSION_MAJOR >= 8
              case Intrinsic::ssub_sat: {
                static const auto WorkaroundMdKind = "beignet.ssub.sat.workaround";
                // Gen needs a workaround when %b == i32 0x80000000
                // %res = call i32 @llvm.ssub.sat.i32(i32 %a, i32 %b)
                // ->
                // r = b == 0x80000000 ? sadd_sat(sadd_sat(a, 0x40000000), 0x40000000) : ssub_sat(a, b)
                //
                // %r0 = call i32 @llvm.ssub.sat.i32(i32 %a, i32 %b)
                // %r1 = call i32 @llvm.sadd.sat.i32(i32 %a, i32 0x40000000)
                // %r2 = call i32 @llvm.sadd.sat.i32(i32 %r1, i32 0x40000000)
                // %p = icmp eq i32 %b, 0x80000000
                // %res = select i1 %p, i32 %r2, %r0
                auto type = CI->getType();
                auto scalarType = dyn_cast<IntegerType>(type->getScalarType());
                GBE_ASSERT(scalarType);
                if(scalarType->getBitWidth() != 32) {
                  break;
                }
                if(CI->getMetadata(WorkaroundMdKind) != nullptr) {
                  break;
                }

                Constant *i32Mid = Builder.getInt32(0x40000000u);
                Constant *i32Min = Builder.getInt32(0x80000000u);

                if(type->isVectorTy()) {
                  i32Mid = ConstantVector::getSplat(type->getVectorNumElements(), i32Mid);
                  i32Min = ConstantVector::getSplat(type->getVectorNumElements(), i32Min);
                }

                auto hwResult = Builder.CreateBinaryIntrinsic(Intrinsic::ssub_sat, CI->getArgOperand(0), CI->getArgOperand(1));
                auto hwBroken = Builder.CreateICmpEQ(CI->getArgOperand(1), i32Min);
                auto waCompute1 = Builder.CreateBinaryIntrinsic(Intrinsic::sadd_sat, i32Mid, CI->getArgOperand(0));
                auto waCompute = Builder.CreateBinaryIntrinsic(Intrinsic::sadd_sat, i32Mid, waCompute1);
                auto result = Builder.CreateSelect(hwBroken, waCompute, hwResult);
                hwResult->setMetadata(WorkaroundMdKind, MDNode::get(Context, {}));

                CI->replaceAllUsesWith(result);
                CI->eraseFromParent();
                changedBlock = true;
                break;
              }
#endif
            default:
              break;
            }
          }
        }
        return changedBlock;
      }
    };

    char GenIntrinsicLowering::ID = 0;

    GenBasicBlockPass *createGenIntrinsicLoweringPass() {
      return new GenIntrinsicLowering();
    }
} // end namespace
