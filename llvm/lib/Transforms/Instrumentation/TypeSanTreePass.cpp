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
#include <cxxabi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FDEBUG_LOG // print debugging information into the file

using namespace llvm;
using std::string;

namespace {

	struct TypeSanTree : public ModulePass {

		static char ID;
		TypeSanTree() : ModulePass(ID) {}

		LLVMContext *Context;

		Type *VoidTy;
		Type *Int8PtrTy;
		Type *Int32PtrTy;
		Type *Int64PtrTy;
		Type *IntptrTy;
		Type *Int64Ty;
		Type *Int32Ty;


                struct ClassInheritanceInfo {

                    StructType *type;
                    bool didProcessFakeHashesBottomUp;
                    bool didProcessFakeHashesTopDown;

                    ClassInheritanceInfo *parent;
                    std::vector<ClassInheritanceInfo*> phantomChildren;
                    
                    uint64_t classHash;
                    std::vector<uint64_t> parentHashes;
                    std::vector<uint64_t> fakeParentHashes;
                        
                    ClassInheritanceInfo(StructType *type) : type(type),
                            didProcessFakeHashesBottomUp(false),
                            didProcessFakeHashesTopDown(false),
                            parent(nullptr) {
                        classHash = TypeSanUtil::getHashCodeFromStruct(type);
                    }
                    
                    void recursiveProcessFakeParentsBottomUp() {
                        if (this->didProcessFakeHashesBottomUp)
                            return;
                        for (ClassInheritanceInfo *childInfo : this->phantomChildren) {
                            childInfo->recursiveProcessFakeParentsBottomUp();
                            // Use set to ensure uniqueness of hashes, without affecting ordering
                            // Duplicate might occur ???
                            std::set<uint64_t> currentHashes(this->fakeParentHashes.begin(), this->fakeParentHashes.end());
                            for (uint64_t hash : childInfo->fakeParentHashes) {
                                auto tryInsertParent = currentHashes.insert(hash);
                                if (tryInsertParent.second) {
                                    this->fakeParentHashes.push_back(hash);
                                }
                            }
                        }
                        this->didProcessFakeHashesBottomUp = true;
                    }
                    
                    void recursiveProcessFakeParentsTopDown() {
                        if (this->didProcessFakeHashesTopDown)
                            return;
                        ClassInheritanceInfo *parentInfo = this->parent;
                        if (parentInfo != nullptr) {
                            parentInfo->recursiveProcessFakeParentsTopDown();
                            // Use set to ensure uniqueness of hashes, without affecting ordering
                            // Duplicate might occur ???
                            std::set<uint64_t> currentHashes(this->fakeParentHashes.begin(), this->fakeParentHashes.end());
                            for (uint64_t hash : parentInfo->fakeParentHashes) {
                                auto tryInsertParent = currentHashes.insert(hash);
                                if (tryInsertParent.second) {
                                    this->fakeParentHashes.push_back(hash);
                                }
                            }
                        }
                        this->didProcessFakeHashesTopDown = true;
                    }
		};

                std::map<StructType*, ClassInheritanceInfo*> classInfoMap;

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

		TargetLibraryInfo *tli;
		typedef std::list<std::pair<long, StructType*> > StructOffsetsTy;
		const DataLayout *DL;

                bool isNewCall(Value *val) {
                        if(isAllocationFn(val, this->tli) && 
                            (isMallocLikeFn(val, this->tli) || isCallocLikeFn(val, this->tli) || !isAllocLikeFn(val, this->tli))) {
                                return true;
                        }

                        return false;
                }

                bool isFreeCallCheck(Value *val) {
                        CallInst *free = isFreeCall(val, this->tli);
                        if(free) {
                                return true;
                        }

                        return false;

                }

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

                void getAnalysisUsage(AnalysisUsage &AU) const override {
                    AU.addRequired<TargetLibraryInfoWrapperPass>();
                }


		////////////////////////////////

		bool doInitialization(Module &M) {

			return true;
		}

		bool doFinalization(Module &M) {

			return false;
		}

                ClassInheritanceInfo *recursiveProcessType(StructType *STy) {
                    if(STy->isLiteral() || STy->isOpaque()) {
                        return nullptr;
                    }
                    // Already processed, return it as it is
                    auto classIt = classInfoMap.find(STy);
                    if (classIt != classInfoMap.end()) {
                        return classIt->second;
                    }
                    ClassInheritanceInfo *info = new ClassInheritanceInfo(STy);
                    // Only check for primary parent class
                    // Secondary parents are handled via offsets
                    if (STy->elements().size() > 0) {
	                    Type * firstElement = *(STy->elements().begin());
	                    if (StructType *InnerSTy = dyn_cast<StructType>(firstElement)) {
	                        ClassInheritanceInfo *parentInfo = recursiveProcessType(InnerSTy);
	                        // Potential parent isn't really a parent ignore it
	                        if (!parentInfo)
	                            return info;
	
	                        info->parent = parentInfo;
	                        
	                        // Insert hashes
	                        info->parentHashes.push_back(parentInfo->classHash);
	                        for (uint64_t hash : parentInfo->parentHashes) {
	                            info->parentHashes.push_back(hash);
	                        }
	                        
	                        // No other members means that it is phantom class
	                        if (STy->elements().size() == 1) {
	                            parentInfo->phantomChildren.push_back(info);
	                            parentInfo->fakeParentHashes.push_back(info->classHash);
	                        }
	                    }
                    }
                    classInfoMap.insert(std::make_pair(STy, info));
                    return info;
                }

		virtual bool runOnModule(Module &M) {

                        Module *SrcM;
                        SrcM = &M;
			size_t found;

                        LLVMContext& Ctx = M.getContext();

			string mname = M.getName();

                        found=mname.find('.');
			mname.erase(found);

			DL = &SrcM->getDataLayout();

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
                        
			std::vector<StructType*> StructTypes;
		        std::vector<StructType*> Types =  SrcM->getIdentifiedStructTypes();

			FunctionType *FTy;

			VoidTy = Type::getInt64Ty(Ctx);
			Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
			Int32PtrTy = PointerType::getUnqual(Type::getInt32Ty(Ctx));
			Int64PtrTy = PointerType::getUnqual(Type::getInt64Ty(Ctx));
			IntptrTy = Type::getInt8PtrTy(Ctx);
			Int64Ty = Type::getInt64Ty(Ctx);
			Int32Ty = Type::getInt32Ty(Ctx);

			TargetLibraryInfoImpl tlii;
			this->tli = &(getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>()->getTLI());

			std::map<CallInst *, Type *> heapObjsFree, heapObjsNew;

			for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
				int index = 0;
				for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
					BasicBlock& b = *BB;
					for (BasicBlock::iterator i = b.begin(), ie = b.end(); i != ie; ++i) {
						// to handle heap allocation
						if (CallInst *call = dyn_cast<CallInst>(&*i)) {
							bool isOverloadedNew = false;
							string functionName = "";
							if (call->getCalledFunction() != nullptr) {
								functionName = call->getCalledFunction()->getName();
							}
							int unmangledStatus;
							char *unmangledName = abi::__cxa_demangle(functionName.c_str(), nullptr, nullptr, &unmangledStatus);
							if (unmangledStatus == 0) {
								string unmangledNameStr(unmangledName);
								if (unmangledNameStr.find("::operator new(unsigned long)") != string::npos ||
									unmangledNameStr.find("::operator new(unsigned long, std::nothrow_t const&)") != string::npos ||
									unmangledNameStr.find("::operator new[](unsigned long)") != string::npos || 
									unmangledNameStr.find("::operator new[](unsigned long, std::nothrow_t const&)") != string::npos) {
										isOverloadedNew = true;
								}
							}
							if(isNewCall(call) || isOverloadedNew) {
								if(Type *allocTy = getMallocAllocatedType(call, this->tli)) {
									if (TypeUtil.interestingType(allocTy)) {
										heapObjsNew.insert(std::pair<CallInst *, Type *>(call, allocTy));
									}
								}
							}
						}
					}
						
					// to handle new 
					for (std::map<CallInst *, Type *>::iterator it=heapObjsNew.begin(); it!=heapObjsNew.end(); ++it) {
						Instruction *next = findNextInstruction(it->first);
						IRBuilder<> Builder(next);

						{
                                                        TypeSanLogger.incTrackedHeap();
							Value *Size;
							unsigned long count = 0;
                            IRBuilder<> PreAllocBuilder(it->first);
							if(isMallocLikeFn(it->first, this->tli) || !isAllocLikeFn(it->first, this->tli) || !isAllocationFn(it->first, this->tli)) {
								if (isMallocLikeFn(it->first, this->tli) || !isAllocationFn(it->first, this->tli)) {
									Size = it->first->getArgOperand(0);
								} else {
									Size = it->first->getArgOperand(1);
								}
								if (ConstantInt *SizeConstant = dyn_cast<ConstantInt>(Size)) {
									count = SizeConstant->getZExtValue() / DL->getTypeAllocSize(it->second);
								} else {
									count = 0;
								}
							} else if (isCallocLikeFn(it->first, this->tli)) {
								Value *NElems = it->first->getArgOperand(0);
								Value *ElemSize = it->first->getArgOperand(1);
                                Size = Builder.CreateMul(NElems, ElemSize);
							} else {
								assert(0 && "Unknown allocation type");
							}

							string allocName = "heap:" + F->getName().str() + ":" + std::to_string(index++) + ":";
							if (it->first->getCalledFunction() != nullptr) {
								allocName += it->first->getCalledFunction()->getName();
							}
							TypeUtil.insertUpdateMetalloc(SrcM, Builder, (Value *)(it->first), it->second, 0, count, Size, allocName);
						}
					}
				}
			}


			// to make class relation information
            for (StructType *STy : Types) {
                if (TypeUtil.interestingType(STy)) {
				    recursiveProcessType(STy);
                }
            }
            for (auto &infoEntry : classInfoMap) {
                infoEntry.second->recursiveProcessFakeParentsBottomUp();
            }
            for (auto &infoEntry : classInfoMap) {
                infoEntry.second->recursiveProcessFakeParentsTopDown();
            }
                        
			Context = &M.getContext();
				 			

			////////////////////////////////////////////////////////// 
			//Insert class's parent information using global variable 
			/////////////////////////////////////////////////////////

                        int classCount = 0;
                        std::vector<Constant*> infoElems;
                        for (auto &infoEntry : classInfoMap) {
                            // No support for anonymous structs yet
                            if (infoEntry.first->isLiteral()) {
                                continue;
                            }
                            // SKip emitting parent sets for non-tracked structs (C-file)
                            // Skip emitting ".base" tmp classes as their hash is identical to the real classes
                            if (!infoEntry.first->getName().startswith("trackedtype.") || infoEntry.first->getName().endswith(".base")) {
                                continue;
                            }
                            classCount++;
                            infoElems.push_back(ConstantInt::get(Int64Ty, infoEntry.second->parentHashes.size() + 1));
                            infoElems.push_back(ConstantInt::get(Int64Ty, infoEntry.second->classHash));
                            for (uint64_t hash : infoEntry.second->parentHashes) {
                                infoElems.push_back(ConstantInt::get(Int64Ty, hash));
                            }
                            if (infoEntry.second->fakeParentHashes.size() > 0) {
                                // New entry for the same class, increase number of "classes"
                                classCount++;
                                infoElems.push_back(ConstantInt::get(Int64Ty, (1 << 31) | (infoEntry.second->fakeParentHashes.size() + 1)));
                                infoElems.push_back(ConstantInt::get(Int64Ty, infoEntry.second->classHash));
                                for (uint64_t hash : infoEntry.second->fakeParentHashes) {
                                    infoElems.push_back(ConstantInt::get(Int64Ty, hash));
                                }
                            }
                        }

                        // No entries in this file, no need to call initializer
                        if (infoElems.size() == 0) {
                            return false;
                        }

                        ArrayType *InfoArrayType = ArrayType::get(Int64Ty, infoElems.size());
                        Constant* infoArray = ConstantArray::get(InfoArrayType, infoElems);
                        GlobalVariable* infoGlobal = new GlobalVariable(M, InfoArrayType, false, 
                                                GlobalVariable::LinkageTypes::InternalLinkage,
                                                nullptr, mname + ".cinfo");
                        infoGlobal->setInitializer(infoArray);
                        
			///////////////////////////////////////////////
			// Update meta-data using global constuctor
			///////////////////////////////////////////////


			FTy = FunctionType::get(Type::getVoidTy(Ctx), false);  
			Function *F = Function::Create(FTy, GlobalValue::InternalLinkage,  
					"__init", SrcM);

			F->setUnnamedAddr(true);                                           
			F->setLinkage(GlobalValue::InternalLinkage);
			F->addFnAttr(Attribute::NoInline);

			BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);             
			IRBuilder<> Builder(BB);

                        string initName = "__update_cinfo";
                        
			Constant *GCOVInit = SrcM->getOrInsertFunction(initName, VoidTy, Int64Ty, Int64PtrTy, nullptr);
			Builder.CreateCall(GCOVInit, {ConstantInt::get(Int64Ty, classCount), Builder.CreatePointerCast(infoGlobal, Int64PtrTy)});
			Builder.CreateRetVoid(); 

			appendToGlobalCtors(M, F, 0); 
			
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
char TypeSanTree::ID = 0;

INITIALIZE_PASS_BEGIN(TypeSanTree, "TypeSanTree",
                "TypeSanPass: fast type safety for C++ programs.",
                false, false);
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(TypeSanTree, "TypeSanTree",
                "TypeSanPass: fast type safety for C++ programs.",
                false, false)

ModulePass *llvm::createTypeSanTreePass() {
  return new TypeSanTree();
}

