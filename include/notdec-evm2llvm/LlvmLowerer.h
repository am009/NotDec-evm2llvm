#pragma once

#include <memory>
#include <string>

#include "llvm/Support/Error.h"

#include "notdec-evm2llvm/TacProgram.h"

namespace llvm {
class LLVMContext;
class Module;
}  // namespace llvm

namespace notdec::evm2llvm {

enum class EvmMemoryModel {
  IntToPtr,
  GlobalArray,
};

struct LlvmLowererConfig {
  std::string ModuleName = "notdec.evm2llvm";
  // IntToPtr is the main NotDec path: EVM memory offsets become native LLVM
  // pointers, so type recovery can see ordinary load/store/inttoptr facts.
  // GlobalArray keeps a synthetic byte array for side-by-side debugging only.
  EvmMemoryModel MemoryModel = EvmMemoryModel::IntToPtr;
};

llvm::Expected<std::unique_ptr<llvm::Module>> lowerToLlvm(
    llvm::LLVMContext &context, const TacProgram &program,
    const LlvmLowererConfig &config);

}  // namespace notdec::evm2llvm
