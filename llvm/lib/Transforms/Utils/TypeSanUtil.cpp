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

#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_set>

using namespace llvm;
using std::string;

#define PAGESHIFT 12
//#define TRACK_ALLOCATIONS

namespace llvm {
        
    TypeSanLoggerClass TypeSanLogger;
    
		static unsigned int crc32c(unsigned char *message) {
			int i, j;
			unsigned int byte, crc, mask;
			static unsigned int table[256];

			/* Set up the table, if necessary. */

			if (table[1] == 0) {
				for (byte = 0; byte <= 255; byte++) {
					crc = byte;
					for (j = 7; j >= 0; j--) {    // Do eight times.
						mask = -(crc & 1);
						crc = (crc >> 1) ^ (0xEDB88320 & mask);
					}
					table[byte] = crc;
				}
			}

			/* Through with table setup, now calculate the CRC. */

			i = 0;
			crc = 0xFFFFFFFF;
			while ((byte = message[i]) != 0) {
				crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF];
				i = i + 1;
			}
			return ~crc;
		}

		uint64_t crc64c(unsigned char *message) {
			int i, j;
			unsigned int byte;
			uint64_t crc, mask;
			static uint64_t table[256];

			/* Set up the table, if necessary. */

			if (table[1] == 0) {
				for (byte = 0; byte <= 255; byte++) {
					crc = byte;
					for (j = 7; j >= 0; j--) {    // Do eight times.
						mask = -(crc & 1);
						crc = (crc >> 1) ^ (0xC96C5795D7870F42UL & mask);
					}
					table[byte] = crc;
				}
			}

			/* Through with table setup, now calculate the CRC. */

			i = 0;
			crc = 0xFFFFFFFFFFFFFFFFUL;
			while ((byte = message[i]) != 0) {
				crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF];
				i = i + 1;
			}
			return ~crc;
		}

		static uint64_t GetHashValue(string str)
		{
			unsigned char *className = new unsigned char[str.length() + 1];
			strcpy((char *)className, str.c_str());

			return crc64c(className);
		}

		static void remove_useless_str(string& str) {

			string::size_type i;

			string basesuffix = ".base";

			while( (i =str.find("*")) != string::npos) {
				str.erase(i, 1);
			}

			while( (i =str.find("'")) != string::npos) {
				str.erase(i, 1);
			}

			i = str.find(basesuffix);

			if (i != string::npos)
				str.erase(i, basesuffix.length());
		}
	
        class StructNode;
        class ArrayNode;

        class TypeNode {
        public:
            virtual StructNode* asStructNode() { return nullptr; }
            virtual ArrayNode* asArrayNode() { return nullptr; }
        };

        class StructNode : public TypeNode {
            static std::map<StructType *, StructNode *> structNodeCache;
            StructNode(StructType *baseType, unsigned long size) : baseType(baseType), size(size) {
                offsets.push_back(std::make_pair(0, this));
            }
        public:
            std::list<std::pair<unsigned long, TypeNode*> > offsets;
            StructType *baseType;
            unsigned long size;
            
            static StructNode* getFromCache(StructType *type) {
                auto iterator = structNodeCache.find(type);
                if (iterator != structNodeCache.end()) {
                    return iterator->second;
                } else {
                    return nullptr;
                }
            }
            static StructNode* create(StructType *baseType, unsigned long size) {
                if (StructNode *existing = StructNode::getFromCache(baseType)) {
                    return existing;
                } else {
                    StructNode *result = new StructNode(baseType, size);
                    structNodeCache.insert(std::make_pair(baseType, result));
                    return result;
                }
            }
            StructNode* asStructNode() override { return this; }
        };
        std::map<StructType *, StructNode *> StructNode::structNodeCache;

        class ArrayNode : public TypeNode {
            static std::map<std::pair<StructNode *, unsigned long>, ArrayNode *> arrayNodeCache;
            ArrayNode(unsigned long count, StructNode *element) : count(count), element(element) {}
        public:
            unsigned long count;
            StructNode *element;
            
            static ArrayNode* create(unsigned long count, TypeNode *element) {
                unsigned long thisCount;
                StructNode *thisElement;
                if (ArrayNode *nestedArray = element->asArrayNode()) {
                    thisCount = count * nestedArray->count;
                    thisElement = nestedArray->element;
                } else if (StructNode *nestedStruct = element->asStructNode()) {
                    thisCount = count;
                    thisElement = nestedStruct;
                } else {
                    assert(0 && "Unsupported element type in ArrayNode constructor");
                }
                auto iterator = arrayNodeCache.find(std::make_pair(thisElement, thisCount));
                if (iterator != arrayNodeCache.end()) {
                    return iterator->second;
                } else {
                    ArrayNode *result = new ArrayNode(thisCount, thisElement);
                    arrayNodeCache.insert(std::make_pair(std::make_pair(thisElement, thisCount), result));
                    return result; 
                }
            }
            ArrayNode* asArrayNode() override { return this; }
        };
        std::map<std::pair<StructNode *, unsigned long>, ArrayNode *> ArrayNode::arrayNodeCache;
        
        static bool isInterestingStructType(StructType *STy) {
            if(!STy->isOpaque()) {
                    return true;
            }
            return false;
        }
        
        static bool isInterestingArrayType(ArrayType *ATy) {
            Type *InnerTy = ATy->getElementType();
            StructType *InnerSTy = dyn_cast<StructType>(InnerTy);
            if (InnerSTy) {
                return isInterestingStructType(InnerSTy);
            }
            ArrayType *InnerATy = dyn_cast<ArrayType>(InnerTy);
            if (InnerATy) {
                return isInterestingArrayType(InnerATy);
            }
            return false;
        }
        
        static TypeNode* getStructLayout(const DataLayout &DL, Type *rootType, std::list<std::pair<unsigned long, TypeNode*> > *nestedOffsets);
        
        static void mergeStructLayout(const DataLayout &DL, Type *innerType, std::list<std::pair<unsigned long, TypeNode*> > *offsets, unsigned long baseOffset) {
            std::list<std::pair<unsigned long, TypeNode*> > nestedOffsets;
            getStructLayout(DL, innerType, &nestedOffsets);
            for (auto &entry : nestedOffsets) {
                // Don't add type-hash for offset 0 (use root type-hash instead)
                if (baseOffset + entry.first != 0) {
                   offsets->push_back(std::make_pair(baseOffset + entry.first, entry.second));
                }
            }
            return;
        }
        
        static TypeNode* getStructLayout(const DataLayout &DL, Type *rootType, std::list<std::pair<unsigned long, TypeNode*> > *nestedOffsets = nullptr) {
            StructType *STy = dyn_cast<StructType>(rootType);
            if(STy && isInterestingStructType(STy)) {
                StructNode *structNode = StructNode::getFromCache(STy);
                if (!structNode) {
                    const StructLayout *SL = DL.getStructLayout(STy);
                    structNode = StructNode::create(STy, DL.getTypeAllocSize(STy));
                    
                    // Traverse all components of struct type
                    unsigned long pos = 0;
                    for (Type *InnerTy : STy->elements()) {
                        StructType *InnerSTy = dyn_cast<StructType>(InnerTy);
                        // Inline all interesting inner structs
                        if (InnerSTy) {
                            if(!isInterestingStructType(InnerSTy)) { continue; }
                            mergeStructLayout(DL, InnerSTy, &(structNode->offsets), SL->getElementOffset(pos));
                        }
                        ArrayType *InnerATy = dyn_cast<ArrayType>(InnerTy);
                        // Inline all intersting inner arrays and create corresponding array nodes
                        if (InnerATy) {
                            ArrayNode *nestedArray = nullptr;
                            if (TypeNode *nestedNode = getStructLayout(DL, InnerATy, nullptr)) {
                                nestedArray = nestedNode->asArrayNode();
                            }
                            if (nestedArray) {
                                // Array at offset 0 / Add first element manually as struct
                                if (SL->getElementOffset(pos) == 0) {
                                    mergeStructLayout(DL, nestedArray->element->baseType, &(structNode->offsets), SL->getElementOffset(pos));
                                    // Add remaining elements of array (as partial node)
                                    if (nestedArray->count > 1) {
                                        ArrayNode *partalNestedArray = ArrayNode::create(nestedArray->count - 1, nestedArray->element);
                                        structNode->offsets.push_back(std::make_pair(partalNestedArray->element->size, partalNestedArray));
                                    }
                                // Array of count 1 / Flatten it into struct
                                } else if (nestedArray->count == 1) {
                                    mergeStructLayout(DL, nestedArray->element->baseType, &(structNode->offsets), SL->getElementOffset(pos));
                                // Add array node in default case
                                } else {
                                    structNode->offsets.push_back(std::make_pair(SL->getElementOffset(pos), nestedArray));
                                }
                            }
                        }
                        pos++;
                    }
                }
                
                // Merge offsets into parent struct node if requested
                if (nestedOffsets != nullptr) {
                    nestedOffsets->insert(nestedOffsets->end(), structNode->offsets.begin(), structNode->offsets.end());
                    return nullptr;
                // Return struct node
                } else {
                    return structNode;
                }
            }
            ArrayType *ATy = dyn_cast<ArrayType>(rootType);
            if (ATy) {
                Type *InnerTy = ATy->getElementType();
                // Nested struct creates new array node if interesting
                StructType *InnerSTy = dyn_cast<StructType>(InnerTy);
                if (InnerSTy) {
                    if(!isInterestingStructType(InnerSTy)) { return nullptr; }
                    return ArrayNode::create(ATy->getNumElements(), getStructLayout(DL, InnerSTy, nullptr));
                }
                // Nested arrays just multiply count field and continue recursively
                ArrayType *InnerATy = dyn_cast<ArrayType>(InnerTy);
                if (InnerATy) {
                    ArrayNode *nestedArray = nullptr;
                    if (TypeNode *nestedNode = getStructLayout(DL, InnerATy, nullptr)) {
                        nestedArray = nestedNode->asArrayNode();
                    }
                    if (nestedArray) {
                        return ArrayNode::create(ATy->getNumElements(), nestedArray);
                    }
                }
            }
            return nullptr;
        }
                
        uint64_t TypeSanUtil::getHashCodeFromStruct(StructType *type) {
            string str;
            if (!type->isLiteral())
                str = type->getName();
            else
                str = "trackedtype._";
            remove_useless_str(str);
            uint64_t hash = GetHashValue(str);
            TypeSanLogger.addHash(hash, str);
            return hash;
        }
        
        static GlobalVariable *getOrPopulateTypeInfo(Module *SrcM, Type *Int64Ty, StructNode *structNode, string &name) {
            if (structNode->baseType->isLiteral()) {
                name = "trackedtype._";
            } else if (structNode->baseType->getName().startswith("trackedtype.")) {
                name = structNode->baseType->getName();
            } else if (structNode->baseType->getName().startswith("blacklistedtype.")) {
                name = structNode->baseType->getName();
            } else {
                name = "noncxxfakesentinel";
            }
            string typeInfoName = "_____typeinfo_____" + name;
            GlobalVariable *typeInfo = SrcM->getNamedGlobal(typeInfoName);
            if (typeInfo)
                return typeInfo;
            std::vector<Constant *> infoMembers;
            infoMembers.push_back(ConstantInt::get(Int64Ty, structNode->size));
            if (!structNode->baseType->isLiteral() && structNode->baseType->getName().startswith("trackedtype.")) {
                unsigned long endOfLastArray = 0;
                for (auto &entry : structNode->offsets) {
                    // Insert a sentinel to signal the end of the last array unless the current element matches the offset
                    if (endOfLastArray != 0 && entry.first != endOfLastArray) {
                        infoMembers.push_back(ConstantInt::get(Int64Ty, endOfLastArray));
                        infoMembers.push_back(ConstantInt::get(Int64Ty, -1));
                        endOfLastArray = 0;
                    }
                    if (StructNode *nestedStruct = entry.second->asStructNode()) {
                        infoMembers.push_back(ConstantInt::get(Int64Ty, entry.first));
                        infoMembers.push_back(ConstantInt::get(Int64Ty, TypeSanUtil::getHashCodeFromStruct(nestedStruct->baseType)));
                    } else {
			string tmpname;
                        ArrayNode *nestedArray = entry.second->asArrayNode();
                        assert(nestedArray && "StructNode member can only be Leaf or Array");
                        infoMembers.push_back(ConstantInt::get(Int64Ty, ((unsigned long)1 << 63) | entry.first));
                        infoMembers.push_back(ConstantExpr::getPtrToInt(getOrPopulateTypeInfo(SrcM, Int64Ty, nestedArray->element, tmpname), Int64Ty));
                        endOfLastArray = entry.first + nestedArray->element->size * nestedArray->count;
                    }
                }
            } else if (structNode->baseType->isLiteral() || !structNode->baseType->getName().startswith("blacklistedtype.")) {
                infoMembers.push_back(ConstantInt::get(Int64Ty, 0));
                infoMembers.push_back(ConstantInt::get(Int64Ty, -1));
            }
            infoMembers.push_back(ConstantInt::get(Int64Ty, -1));
            ArrayType *TypeInfoTy = ArrayType::get(Int64Ty, infoMembers.size());
            typeInfo = new GlobalVariable(*SrcM, TypeInfoTy, false, 
                                            GlobalVariable::LinkageTypes::InternalLinkage,
                                            nullptr, typeInfoName);
            Constant *initializer = ConstantArray::get(TypeInfoTy, infoMembers);
            typeInfo->setInitializer(initializer);
            // Compute hash-code for current node for Logger
            TypeSanUtil::getHashCodeFromStruct(structNode->baseType);
            return typeInfo;
        }
        
	bool TypeSanUtil::interestingType(Type *rootType) {

		StructType *STy = dyn_cast<StructType>(rootType);
		if(STy) {
                    return isInterestingStructType(STy);
		}
		ArrayType *ATy = dyn_cast<ArrayType>(rootType);
                if (ATy) {
                    return isInterestingArrayType(ATy);
                }
		return false;
	}

	
	void TypeSanUtil::insertUpdateMetalloc(Module *SrcM, IRBuilder<> &Builder, Value *ptrValue, Type *allocationType, int alignment, unsigned long count, Value *size, string allocName) {
                Value *ptrToInt = Builder.CreatePtrToInt(ptrValue, Int64Ty);

                TypeNode *typeNode = getStructLayout(DL, allocationType, nullptr);
                StructNode *structNode = nullptr;
                if (count != 0) {
                    ArrayNode *tmpArrayNode = ArrayNode::create(count, typeNode);
                    structNode = tmpArrayNode->element;
                    count = tmpArrayNode->count;
                } else {
                    structNode = typeNode->asStructNode();
                    if (structNode == nullptr) {
                        structNode = typeNode->asArrayNode()->element;
                    }
                }
		string typeName;
                GlobalVariable *typeInfo = getOrPopulateTypeInfo(SrcM, Int64Ty, structNode, typeName);
                Value *ptrToStore;
                // No support for anonymous structs yet
                if (structNode->baseType->isLiteral()) {
                    ptrToStore = ConstantInt::get(Int64Ty, 0);
                } else {
                    ptrToStore = ptrToInt;
                }
                
		Value *pageIdx = Builder.CreateLShr(ptrToInt, ConstantInt::get(Int64Ty, PAGESHIFT));
		Value *pageTableBase = Builder.CreateIntToPtr(MetaPageTable, Int64PtrTy);
		Value *pageTablePtr = Builder.CreateGEP(pageTableBase, pageIdx);
		Value *pageTableEntry = Builder.CreateLoad(pageTablePtr);
		Value *metadataBaseInt = Builder.CreateLShr(pageTableEntry, ConstantInt::get(Int64Ty, 8));
		Value *alignmentValue = (alignment != 0) ? ConstantInt::get(Int64Ty, alignment) : Builder.CreateAnd(pageTableEntry, ConstantInt::get(Int64Ty, 0xFF));
		Value *alignmentOffset = (alignment != 0) ? ConstantInt::get(Int64Ty, (1 << alignment) - 1) : Builder.CreateSub(Builder.CreateShl(
			ConstantInt::get(Int64Ty, 1), alignmentValue), ConstantInt::get(Int64Ty, 1));
		Value *metadataBase = Builder.CreateIntToPtr(metadataBaseInt, Int64PtrTy);
		Value *pageOffset = Builder.CreateAnd(ptrToInt, ConstantInt::get(Int64Ty, (1 << PAGESHIFT) - 1));
		Value *metadataOffset = Builder.CreateShl(Builder.CreateLShr(pageOffset, alignmentValue), 1);
		Value *metadataPtr = Builder.CreateGEP(metadataBase, metadataOffset);
		// Inline stores if size and alignment are known constants (stack/globals)
		bool didInline = false;
		if (count != 0 && alignment != 0) {
			long constantSize = ((structNode->size * count) + ((1 << alignment) - 1)) >> alignment;
			if (constantSize <= 16) {
				didInline = true;
                                Value *typeInfoPtrInt = nullptr;
                                if (count == 1) {
                                     typeInfoPtrInt = ConstantExpr::getAdd(ConstantExpr::getPtrToInt(typeInfo, Int64Ty), ConstantInt::get(Int64Ty, 8));
                                } else {
                                    typeInfoPtrInt = ConstantExpr::getPtrToInt(typeInfo, Int64Ty);
                                }
                                Builder.CreateStore(ptrToStore, metadataPtr);
                                Value *metadataPtr2 = Builder.CreateGEP(metadataPtr, ConstantInt::get(Int64Ty, 1));
                                Builder.CreateStore(typeInfoPtrInt, metadataPtr2);
                                unsigned long offset = alignment;
				for (int i = 1; i < constantSize; ++i) {
					Value *metadataPtrWithIndex = Builder.CreateGEP(metadataPtr, ConstantInt::get(Int64Ty, 2 * i));
                                        Builder.CreateStore(ptrToStore, metadataPtrWithIndex);
					Value *metadataPtrWithIndex2 = Builder.CreateGEP(metadataPtr, ConstantInt::get(Int64Ty, 2 * i + 1));
                                        Builder.CreateStore(typeInfoPtrInt, metadataPtrWithIndex2);
                                        offset += alignment;
                                }
			}
                }
		// Call out to helper if no inlining occured
		if (!didInline) {
                        Value *metadataSize;
                        Value *typeInfoPtrInt = nullptr;
			if (count == 0) {
				metadataSize = Builder.CreateLShr(Builder.CreateAdd(size, alignmentOffset), alignmentValue);
                                typeInfoPtrInt = ConstantExpr::getPtrToInt(typeInfo, Int64Ty);
			} else {
				metadataSize = Builder.CreateLShr(Builder.CreateAnd(Builder.CreateAdd(ConstantInt::get(Int64Ty, structNode->size * count), alignmentOffset),
                                                                            ConstantInt::get(Int64Ty, ((unsigned long)1 << 63) - 1)),
                                                        alignmentValue);
                                if (count == 1) {
                                    typeInfoPtrInt = ConstantExpr::getAdd(ConstantExpr::getPtrToInt(typeInfo, Int64Ty), ConstantInt::get(Int64Ty, 8));
                                } else {
                                    typeInfoPtrInt = ConstantExpr::getPtrToInt(typeInfo, Int64Ty);
                                }
			}
			Function *MetallocMemset = (Function*)SrcM->getOrInsertFunction("metalloc_widememset", VoidTy, Int64PtrTy, Int64Ty, Int64Ty, Int64Ty, nullptr);
			Value *Param[4] = {metadataPtr, metadataSize, ptrToStore, typeInfoPtrInt};
			Builder.CreateCall(MetallocMemset, Param);
                }

#ifdef TRACK_ALLOCATIONS
		static GlobalVariable* gvar_int64_typecasts;
		if (!gvar_int64_typecasts) {
			gvar_int64_typecasts = new GlobalVariable(/*Module=*/*SrcM,
			/*Type=*/IntegerType::get(SrcM->getContext(), 64),
			/*isConstant=*/false,
			/*Linkage=*/GlobalValue::ExternalLinkage,
			/*Initializer=*/0,
			/*Name=*/"__typesan_alloc_count");
			gvar_int64_typecasts->setAlignment(8);
		}
                LoadInst* typecasts = Builder.CreateLoad(gvar_int64_typecasts);
                ConstantInt* one = ConstantInt::get(SrcM->getContext(), APInt(64, StringRef("1"), 10));
                Value* typecasts_inc = Builder.CreateAdd(typecasts, one);
                Builder.CreateStore(typecasts_inc, gvar_int64_typecasts);
#endif
	}

}

