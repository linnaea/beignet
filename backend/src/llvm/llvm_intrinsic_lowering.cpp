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
    class IntrinsicLowering : public BasicBlockPass
    {
    public:
      static char ID;
      IntrinsicLowering() :
        BasicBlockPass(ID) {}

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
      virtual bool runOnBasicBlock(BasicBlock &BB)
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
                break;
              }
#if LLVM_VERSION_MAJOR >= 8
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
                auto operandBits = CI->getOperand(0)->getType()->getScalarSizeInBits();
                Constant *nBits = Builder.getIntN(operandBits, operandBits);
                if(CI->getType()->isVectorTy()) {
                  nBits = ConstantVector::getSplat(CI->getType()->getVectorNumElements(), nBits);
                }

                auto realShift = Builder.CreateURem(CI->getOperand(2), nBits);
                auto funShift = Builder.CreateSub(nBits, realShift);

                if(intrinsicID == Intrinsic::fshr) {
                  std::swap(realShift, funShift);
                }

                auto upperBits = Builder.CreateShl(CI->getOperand(0), realShift);
                auto lowerBits = Builder.CreateLShr(CI->getOperand(1), funShift);
                auto combined = Builder.CreateOr(upperBits, lowerBits);

                CI->replaceAllUsesWith(combined);
                CI->eraseFromParent();
                break;
              }
#endif
#if LLVM_VERSION_MAJOR >= 9
              case Intrinsic::usub_sat: {
                // %res = call T @llvm.usub.sat.T(T %a, T %b)
                // -> max(a, b) - b
                //
                // %pred = icmp ugt T %a %b
                // %op0 = select i1 %pred, T %a, T %b
                // %res = sub T %op0, %b
                auto pred = Builder.CreateICmpUGT(CI->getOperand(0), CI->getOperand(1));
                auto op0 = Builder.CreateSelect(pred, CI->getOperand(0), CI->getOperand(1));
                auto res = Builder.CreateSub(op0, CI->getOperand(1));

                CI->replaceAllUsesWith(res);
                CI->eraseFromParent();
                break;
              }
              case Intrinsic::ssub_sat: {
                // %res = call T @llvm.ssub.sat.T(T %a, T %b)
                // ->
                // s = a - b
                // overflow = (a ^ b) & (a ^ s)
                // bound = (Tmask >>> 1) + (a >>> (Tbits - 1))
                // res = overflow ? bound : s
                //
                // %sub = sub T %a, %b
                // %asign = lshr T %a, Sub(Tbits, 1)
                // %bound = add T %asign, LShr(Tmask, 1)
                // %ovf1 = xor T %a, %b
                // %ovf2 = xor T %a, %sub
                // %ovf3 = and T %ovf1, %ovf2
                // %ovf4 = lshr T %ovf3, Sub(Tbits, 1)
                // %ovf = trunc %ovf4 to i1
                // %res = select i1 %ovf, T %bound, T %sub
                auto type = CI->getType();
                auto scalarType = dyn_cast<IntegerType>(type->getScalarType());
                GBE_ASSERT(scalarType);
                auto scalarMax = scalarType->getMask();
                scalarMax.lshrInPlace(1);

                Constant *tMaxPos = Builder.getInt(scalarMax);
                Constant *tBitsM1 = Builder.getIntN(scalarType->getBitWidth(), scalarType->getBitWidth() - 1);
                Type *tyI1 = Builder.getInt1Ty();
                if(type->isVectorTy()) {
                  tMaxPos = ConstantVector::getSplat(type->getVectorNumElements(), tMaxPos);
                  tBitsM1 = ConstantVector::getSplat(type->getVectorNumElements(), tBitsM1);
                  tyI1 = VectorType::get(tyI1, type->getVectorNumElements());
                }

                auto sub = Builder.CreateSub(CI->getOperand(0), CI->getOperand(1));
                auto aSign = Builder.CreateLShr(CI->getOperand(0), tBitsM1);
                auto aBound = Builder.CreateAdd(aSign, tMaxPos);

                auto canOverflow = Builder.CreateXor(CI->getOperand(0), CI->getOperand(1));
                auto mayOverflow = Builder.CreateXor(CI->getOperand(0), sub);
                auto didOverflow = Builder.CreateAnd(canOverflow, mayOverflow);
                didOverflow = Builder.CreateLShr(didOverflow, tBitsM1);
                didOverflow = Builder.CreateTrunc(didOverflow, tyI1);
                auto result = Builder.CreateSelect(didOverflow, aBound, sub);

                CI->replaceAllUsesWith(result);
                CI->eraseFromParent();
                break;
              }
#endif
              default:
                continue;
            }
          }
        }
        return changedBlock;
      }
    };

    char IntrinsicLowering::ID = 0;

    BasicBlockPass *createIntrinsicLoweringPass() {
      return new IntrinsicLowering();
    }
} // end namespace
