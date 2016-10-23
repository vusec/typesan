#ifndef LLVM_TRANSFORMS_UTILS_TYPESAN_H
#define LLVM_TRANSFORMS_UTILS_TYPESAN_H

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>

using namespace llvm;
using std::string;

namespace llvm {

	typedef std::list<std::pair<long, StructType*> > StructOffsetsTy;

	class DataLayout;
	class Function;
	class TargetLibraryInfo;

    class TypeSanLoggerClass {
        std::map<uint64_t, string> hashMapping;
        unsigned long staticDownCasts = 0;
        unsigned long trackedStack = 0;
        unsigned long trackedGlobal = 0;
        unsigned long trackedHeap = 0;
    public:
        TypeSanLoggerClass() {
        }
        ~TypeSanLoggerClass() {
            string logFile = "";
            char *envVariable = getenv("TYPECHECK_LOGFILE");
            if (envVariable != nullptr) {
                logFile = envVariable;
            }
            if (logFile != "") {
                sem_t *loggerSemaphore = sem_open("TYPECHECK_LOGGER_SEMAPHORE", O_CREAT, 0600, 1);
                sem_wait(loggerSemaphore);
                std::ifstream inputFile(logFile);
                if (inputFile.is_open()) {
                    string line;
                    
                    std::getline(inputFile, line);
                    staticDownCasts += stoul(line);
                    std::getline(inputFile, line);
                    trackedStack += stoul(line);
                    std::getline(inputFile, line);
                    trackedGlobal += stoul(line);
                    std::getline(inputFile, line);
                    trackedHeap += stoul(line);
                    
                    while(1) {
                        std::getline(inputFile, line);
                        // If a line was successfully read
                        if(inputFile.good()) {
                            uint64_t hash = (uint64_t)stoul(line);
                            std::getline(inputFile, line);
                            hashMapping.insert(std::make_pair(hash, line));
                        // No valid line read, meaning we reached the end of file
                        } else {
                            break;
                        }
                    }
                    
                    inputFile.close();
                }
                std::ofstream outputFile(logFile);
                outputFile << staticDownCasts << std::endl;
                outputFile << trackedStack << std::endl;
                outputFile << trackedGlobal << std::endl;
                outputFile << trackedHeap << std::endl;
                for (auto &hashMappingEntry : hashMapping) {
                    outputFile << hashMappingEntry.first << std::endl;
                    outputFile << hashMappingEntry.second << std::endl;
                }
                outputFile.close();
                sem_post(loggerSemaphore);
                sem_close(loggerSemaphore);
                sem_unlink("TYPECHECK_LOGGER_SEMAPHORE");
            }
        }
        void addHash(uint64_t hashCode, string &name) {
            hashMapping.insert(std::make_pair(hashCode, name));
        }
        void incStaticDownCast() {
            staticDownCasts++;
        }
        void incTrackedStack() {
            trackedStack++;
        }
        void incTrackedGlobal() {
            trackedGlobal++;
        }
        void incTrackedHeap() {
            trackedHeap++;
        }
        
    };
    
    extern TypeSanLoggerClass TypeSanLogger;
        
	class TypeSanUtil {
		public:
			TypeSanUtil(const DataLayout &DL)
				: DL(DL) {
			}

			Type *VoidTy;
			Type *Int8PtrTy;
			Type *Int32PtrTy;
			Type *Int64PtrTy;
			Type *IntptrTy;
			Type *Int64Ty;
			Type *Int32Ty;

                        ArrayType *MetadataTy;
			Constant *MetaPageTable;
                        
			void insertUpdateMetalloc(Module *SrcM, IRBuilder<> &Builder, Value *ptrValue, Type *allocationType, int alignment, unsigned long count, Value *size, string allocName);
			bool interestingType(Type *rootType);
			static uint64_t getHashCodeFromStruct(StructType *STy);

			const DataLayout &DL;

	};

} // llvm namespace

#endif  // LLVM_TRANSFORMS_UTILS_TYPESAN_H

