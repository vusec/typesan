#include "llvm/Analysis/CallGraph.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Transforms/Utils/TypeSanUtil.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NODE 1000
#define MAX_PNUM 50
#define MAX_NAME 500

#define FDEBUG_LOG // print debugging information into the file

using namespace llvm;
using std::string;

typedef std::list<std::pair<long, StructType*> > StructOffsetsTy;

namespace {

	struct TypeSan : public ModulePass {

		static char ID;
		TypeSan() : ModulePass(ID) {}

		const DataLayout *DL;
		TargetLibraryInfo *TLI;
		CallGraph *CG;

		Type *VoidTy;
		Type *Int8PtrTy;
		Type *Int32PtrTy;
		Type *Int64PtrTy;
		Type *IntptrTy;
		Type *Int64Ty;
		Type *Int32Ty;
    		Type *Int1Ty;

		FunctionType *VoidFTy;

		ArrayType *MetadataTy;                

		Constant *MetaPageTable;

                std::map<Function*, bool> mayCastMap;
                std::set<Function*> functionsVisitedForMayCast;
                
		///////////////////////
		// Declare function 
                //////////////////////

		#ifdef FDEBUG_LOG
		void write_flog(string result) {
			FILE *op = fopen("pass_result.txt", "a");
			fprintf(op, "%s\n", result.c_str());
			fclose(op);
		}
		#endif

		Instruction* findNextInstruction(Instruction *inst) {
			BasicBlock::iterator it(inst);
			++it;
			if(it == inst->getParent()->end()) {
				/* Inst is the last instruction in the basic block */
				return NULL;
			} else {
				return &*it;
			}
		}
		
		void getAnalysisUsage(AnalysisUsage &Info) const {
			Info.addRequired<CallGraphWrapperPass>();
		}

		bool doInitialization(Module &M) {

			return true;
		}

		bool doFinalization(Module &M) {

			return false;
		}

                bool mayCast(Function *F, std::set<Function*> &visited, bool *isComplete) {
                    // Externals may cast
                    if (F->isDeclaration()) {
                        return true;
                    }
                    
                    // Check previously processed
                    auto mayCastIterator = mayCastMap.find(F);
                    if (mayCastIterator != mayCastMap.end()) {
                        return mayCastIterator->second;
                    }
                    
                    visited.insert(F);
                    
                    bool isCurrentComplete = true;
                    
                    for (auto &I : *(*CG)[F]) {
                        Function *calleeFunction = I.second->getFunction();
                        // Default to true to avoid accidental bugs on API changes
                        bool result = true;
                        // Indirect call
                        if (!calleeFunction) {
                            result = true;
                        // Recursion detected, do not process callee
                        } else if (visited.count(calleeFunction)) {
                            isCurrentComplete = false;
                        // Explicit call to checker
                        } else if (calleeFunction->getName().find("type_casting_verification") != StringRef::npos) {
                            TypeSanLogger.incStaticDownCast();
                            result = true;
                        // Check recursively
                        } else {
                            bool isCalleeComplete;
                            result = mayCast(calleeFunction, visited, &isCalleeComplete);
                            // Forbid from caching if callee was not complete (due to recursion)
                            isCurrentComplete &= isCalleeComplete;
                        }
                        // Found a potentialy cast, report true
                        if (result) {
                            // Cache and report even if it was incomplete
                            // Missing traversal can never flip it to not found
                            mayCastMap.insert(std::make_pair(F, true));
                            *isComplete = true;
                            return true;
                        }
                    }
                    
                    // No cast found anywhere, report false
                    // Do not cache negative results if current traversal was not complete (due to recursion)
                    /*if (isCurrentComplete) {
                        mayCastMap.insert(std::make_pair(F, false));
                    }*/
                    // Report to caller that this traversal was incomplete
                    *isComplete = isCurrentComplete;
                    return false;
                }
		
		virtual bool runOnModule(Module &M) {

			Module *SrcM = &M;
			// Need to keep method-local as it is reference (LLVM API limitations)
			LLVMContext &Ctx = (M.getContext());
			// Initialize members to be used in helpers
			DL = &SrcM->getDataLayout();
                        
                        CG = &getAnalysis<CallGraphWrapperPass>().getCallGraph();
                        
			TypeSanUtil TypeUtil(*DL);

			TypeUtil.VoidTy = Type::getInt64Ty(Ctx);
			TypeUtil.Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
			TypeUtil.Int32PtrTy = PointerType::getUnqual(Type::getInt32Ty(Ctx));
			TypeUtil.Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));
			TypeUtil.IntptrTy = Type::getInt8PtrTy(Ctx);
			TypeUtil.Int64Ty = Type::getInt64Ty(Ctx);
			TypeUtil.Int32Ty = Type::getInt32Ty(Ctx);
  
			TypeUtil.MetadataTy = ArrayType::get(TypeUtil.Int64Ty, 2);
			TypeUtil.MetaPageTable = ConstantInt::get(TypeUtil.Int64Ty, 0x400000000000);
                        
			TargetLibraryInfoImpl tlii;
			TLI = new TargetLibraryInfo(tlii);
			VoidTy = Type::getInt64Ty(Ctx);
			Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
			Int32PtrTy = PointerType::getUnqual(Type::getInt32Ty(Ctx));
			Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));
			IntptrTy = Type::getInt8PtrTy(Ctx);
			Int64Ty = Type::getInt64Ty(Ctx);
			Int32Ty = Type::getInt32Ty(Ctx);
			Int1Ty = Type::getInt1Ty(Ctx);

                        mayCastMap.clear();
                        functionsVisitedForMayCast.clear();
                        
                        //declare void @llvm.memcpy.p0i8.p0i8.i64(i8 *, i8 *, i64, i32, i1)
                        Type *MemcpyParams[] = { Int8PtrTy, Int8PtrTy, Int64Ty };
                        Function *MemcpyFunc = Intrinsic::getDeclaration(SrcM, Intrinsic::memcpy, MemcpyParams);
                        
			VoidFTy = FunctionType::get(Type::getVoidTy(Ctx), false);

			MetadataTy = ArrayType::get(Int64Ty, 2);                      

			Function *FGlobal = Function::Create(VoidFTy, GlobalValue::InternalLinkage,
					"__init_global_object" + M.getName(), SrcM);

			FGlobal->setUnnamedAddr(true);
			FGlobal->setLinkage(GlobalValue::InternalLinkage);
			FGlobal->addFnAttr(Attribute::NoInline);

			BasicBlock *BBGlobal = BasicBlock::Create(Ctx, "entry", FGlobal);
			IRBuilder<> BuilderGlobal(BBGlobal);

			std::list<GlobalVariable *> trackedGlobals;

			// Find interesting globalvariables
			for (GlobalVariable &GV : M.globals()) {
				if (GV.getName() == "llvm.global_ctors" ||
					GV.getName() == "llvm.global_dtors" ||
					GV.getName() == "llvm.global.annotations" ||
					GV.getName() == "llvm.used") {
					continue;
				}
				if (TypeUtil.interestingType(GV.getValueType())) {
					trackedGlobals.push_back(&GV);
				}
			}

			if (trackedGlobals.size() > 0) {
                            Function *FMGlobal = (Function*)M.getOrInsertFunction("metalloc_init_globals", VoidTy, Int64Ty, nullptr);
                            Value *FMGlobalParam[1] = { BuilderGlobal.CreatePtrToInt(*(trackedGlobals.begin()), Int64Ty) };
                            BuilderGlobal.CreateCall(FMGlobal, FMGlobalParam);
                        }
			
			// To save globalvariable information
			for (GlobalVariable *GV : trackedGlobals) {
                                        TypeSanLogger.incTrackedGlobal();
					string allocName = "global:" + GV->getName().str();
					TypeUtil.insertUpdateMetalloc(SrcM, BuilderGlobal, GV, GV->getValueType(), 3, 1, ConstantInt::get(Int64Ty, DL->getTypeAllocSize(GV->getValueType())), allocName);
			}

			BuilderGlobal.CreateRetVoid();

			// to insert into GlobalCtor
			appendToGlobalCtors(M, FGlobal, 0);

			for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
				if (F->empty() || F->getEntryBlock().empty() || F->getName().startswith("__init_global_object")) {
					continue;
				}
				if (getenv("TYPECHECK_DISABLE_STACK_OPT") == nullptr) {
					std::set<Function*> visitedFunctions;
					bool tmp;
					bool mayCurrentCast = mayCast(&*F, visitedFunctions, &tmp);
					// Insert result into cache, since result is final at this point
					mayCastMap.insert(std::make_pair(&*F, mayCurrentCast));
					if (!mayCurrentCast) {
						continue;
					}
				}
				for (auto &a : F->args()) {
					Argument *Arg = dyn_cast<Argument>(&a);
					if (!Arg->hasByValAttr()) {
						continue;
					}
					Type *ArgPointedTy = Arg->getType()->getPointerElementType();
					if (TypeUtil.interestingType(ArgPointedTy)) {
						unsigned long size = DL->getTypeStoreSize(ArgPointedTy);
                                                IRBuilder<> B(&*(F->getEntryBlock().getFirstInsertionPt()));
                                                Value *NewAlloca = B.CreateAlloca(ArgPointedTy);
                                                Arg->replaceAllUsesWith(NewAlloca);
                                                Value *Src = B.CreatePointerCast(Arg, Int8PtrTy);
                                                Value *Dst = B.CreatePointerCast(NewAlloca, Int8PtrTy);
                                                Value *Param[5] = { Dst, Src, ConstantInt::get(Int64Ty, size), 
                                                        ConstantInt::get(Int32Ty, 1), ConstantInt::get(Int1Ty, 0) };
                                                B.CreateCall(MemcpyFunc, Param);
					}
				}
				std::list<AllocaInst *> trackedAllocas;
				for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
					BasicBlock& b = *BB;

					for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {

						IRBuilder<> Builder(&*i);

						if (AllocaInst *AI = dyn_cast<AllocaInst>(&*i)) {
							if (TypeUtil.interestingType(AI->getAllocatedType())) {
								trackedAllocas.push_back(AI);
							}
						}
					}
				}

				int index = 0;
				Function *index_func = nullptr;
				for (AllocaInst *AI : trackedAllocas) {
					Instruction *next = findNextInstruction(AI);
					IRBuilder<> Builder(next);
					{
						if (index_func == AI->getFunction()) {
							index++;
						} else {
							index = 0;
							index_func = AI->getFunction();
						}
						string allocName = "stack:" + AI->getFunction()->getName().str() + ":" + std::to_string(index);
						
                                                TypeSanLogger.incTrackedStack();
						MDNode *node = MDNode::get(Ctx, MDString::get(Ctx, "trackedalloca"));
						AI->setMetadata("TrackedAlloca", node);
                        if (ConstantInt *constantSize = dyn_cast<ConstantInt>(AI->getArraySize())) {
    						TypeUtil.insertUpdateMetalloc(SrcM, Builder, AI, AI->getAllocatedType(), 6, constantSize->getZExtValue(), 
                                ConstantExpr::getMul(ConstantInt::get(Int64Ty, constantSize->getZExtValue()), ConstantInt::get(Int64Ty, DL->getTypeAllocSize(AI->getAllocatedType()))), allocName);
                        } else {
                        			Value *arraySize = AI->getArraySize();
                        			if (arraySize->getType() != Int64Ty) {
							arraySize = Builder.CreateIntCast(arraySize, Int64Ty, false);
                        			}
    						TypeUtil.insertUpdateMetalloc(SrcM, Builder, AI, AI->getAllocatedType(), 6, 0, 
                                Builder.CreateMul(arraySize, ConstantInt::get(Int64Ty, DL->getTypeAllocSize(AI->getAllocatedType()))), allocName);
                        }
					}
				}
			}

			return false;
		}

		virtual bool runOnFunction(Function &F) {

			return false;
		}

		bool runOnBasicBlock(BasicBlock &BB) {

			return true;
		}
	};
}

//register pass
char TypeSan::ID = 0;

INITIALIZE_PASS_BEGIN(TypeSan, "TypeSan",
                "TypeUtilPass: fast type safety for C++ programs.",
                false, false)
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
INITIALIZE_PASS_END(TypeSan, "TypeSan",
                "TypeSanPass: fast type safety for C++ programs.",
                false, false)

ModulePass *llvm::createTypeSanPass() {
  return new TypeSan();
}

