#pragma once

#include <map>
#include <string>
#include <vector>

#include "llvm/ADT/APInt.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Error.h"

#include "notdec-evm2llvm/LlvmLowerer.h"
#include "notdec-evm2llvm/TacProgram.h"

namespace llvm {
class Function;
class GlobalVariable;
class LLVMContext;
class Type;
class Value;
}  // namespace llvm

namespace notdec::evm2llvm {

struct RuntimeHandles {
  llvm::Value *Mem = nullptr;
  llvm::Value *Calldata = nullptr;
  llvm::Value *Returndata = nullptr;
  llvm::Value *Env = nullptr;
};

// Lowers one TAC instruction at a time into the current LLVM basic block.
// Values is the TAC scalar SSA state. PHI defs are pre-created by LlvmLowerer,
// so loop-carried uses can reference them before the incoming edges are filled.
class InstructionLowerer {
 public:
  InstructionLowerer(llvm::IRBuilder<> &builder, llvm::LLVMContext &context,
                     llvm::Type *wordType,
                     std::map<FactId, llvm::Value *> &values,
                     const TacProgram &program, RuntimeHandles handles,
                     EvmMemoryModel memoryModel);

  llvm::Error lower(const TacStatement &stmt);
  llvm::Expected<llvm::Value *> loadWord(const FactId &var);
  llvm::Error defineWord(const FactId &var, llvm::Value *value);
  llvm::APInt parseWordConstant(const std::string &text) const;

 private:
  llvm::Expected<llvm::Value *> lowerUnary(const TacStatement &stmt);
  llvm::Expected<llvm::Value *> lowerBinary(const TacStatement &stmt);
  llvm::Expected<llvm::Value *> lowerTernary(const TacStatement &stmt);
  llvm::Expected<llvm::Value *> lowerStateRead(const TacStatement &stmt);
  llvm::Error lowerStateWrite(const TacStatement &stmt);
  llvm::Expected<std::vector<llvm::Value *>> loadOperands(const TacStatement &stmt,
                                                          unsigned expectedCount);
  llvm::Value *boolToWord(llvm::Value *value);
  llvm::Value *memoryPointer(llvm::Value *address);
  llvm::GlobalVariable *memoryGlobal();
  llvm::Function *runtimeFunction(const char *name);

  llvm::IRBuilder<> &Builder;
  llvm::LLVMContext &Context;
  llvm::Type *WordType;
  std::map<FactId, llvm::Value *> &Values;
  const TacProgram &Program;
  RuntimeHandles Handles;
  EvmMemoryModel MemoryModel;
  llvm::GlobalVariable *Memory = nullptr;
};

}  // namespace notdec::evm2llvm
