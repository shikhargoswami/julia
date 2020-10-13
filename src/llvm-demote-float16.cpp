// This file is a part of Julia. License is MIT: https://julialang.org/license

#include "llvm-version.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Debug.h>

#define DEBUG_TYPE "demote_float16"

using namespace llvm;

namespace {

struct DemoteFloat16Pass : public FunctionPass {
    static char ID;
    DemoteFloat16Pass() : FunctionPass(ID){};

private:
    bool runOnFunction(Function &F) override;
};

bool DemoteFloat16Pass::runOnFunction(Function &F)
{
    auto &ctx = F.getContext();
    auto T_float16 = Type::getHalfTy(ctx);
    auto T_float32 = Type::getFloatTy(ctx);

    SmallVector<Instruction *, 0> erase;
    for (auto &BB : F) {
        for (auto &I : BB) {
            switch (I.getOpcode()) {
            case Instruction::FAdd:
            case Instruction::FSub:
            case Instruction::FMul:
            case Instruction::FDiv:
            case Instruction::FRem:
                break;
            default:
                continue;
            }

            IRBuilder<> builder(&I);

            // extend Float16 operands to Float32
            bool OperandsChanged = false;
            SmallVector<Value *, 2> Operands(I.getNumOperands());
            for (size_t i = 0; i < I.getNumOperands(); i++) {
                Value *Op = I.getOperand(i);
                if (Op->getType() == T_float16) {
                    Op = builder.CreateFPExt(Op, T_float32);
                    OperandsChanged = true;
                }
                Operands[i] = (Op);
            }

            // recreate the instruction if any operands changed,
            // truncating the result back to Float16
            if (OperandsChanged) {
                Value *NewI;
                switch (I.getOpcode()) {
                case Instruction::FAdd:
                    assert(Operands.size() == 2);
                    NewI = builder.CreateFAddFMF(Operands[0], Operands[1], &I);
                    break;
                case Instruction::FSub:
                    assert(Operands.size() == 2);
                    NewI = builder.CreateFSubFMF(Operands[0], Operands[1], &I);
                    break;
                case Instruction::FMul:
                    assert(Operands.size() == 2);
                    NewI = builder.CreateFMulFMF(Operands[0], Operands[1], &I);
                    break;
                case Instruction::FDiv:
                    assert(Operands.size() == 2);
                    NewI = builder.CreateFDivFMF(Operands[0], Operands[1], &I);
                    break;
                case Instruction::FRem:
                    assert(Operands.size() == 2);
                    NewI = builder.CreateFRemFMF(Operands[0], Operands[1], &I);
                    break;
                default:
                    abort();
                }
                ((Instruction *)NewI)->copyMetadata(I);
                if (NewI->getType() != I.getType())
                    NewI = builder.CreateFPTrunc(NewI, I.getType());
                I.replaceAllUsesWith(NewI);
                erase.push_back(&I);
            }
        }
    }

    if (erase.size() > 0) {
        for (auto V : erase)
            V->eraseFromParent();
        return true;
    }
    else
        return false;
}

char DemoteFloat16Pass::ID = 0;
static RegisterPass<DemoteFloat16Pass>
        Y("DemoteFloat16",
          "Demote Float16 operations to Float32 equivalents.",
          false,
          false);
}

Pass *createDemoteFloat16Pass()
{
    return new DemoteFloat16Pass();
}
