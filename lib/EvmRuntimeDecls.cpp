#include "notdec-evm2llvm/EvmRuntimeDecls.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

namespace notdec::evm2llvm {

void declareEvmRuntimeHelpers(llvm::Module &module) {
  auto &context = module.getContext();
  auto *wordType = llvm::Type::getIntNTy(context, 256);
  auto *ptrType = llvm::PointerType::get(context, 0);
  auto *voidType = llvm::Type::getVoidTy(context);

  module.getOrInsertFunction("evm_mcopy", voidType, ptrType, wordType, wordType,
                             wordType);
  module.getOrInsertFunction("evm_msize", wordType, ptrType);
  module.getOrInsertFunction("evm_sload", wordType, wordType);
  module.getOrInsertFunction("evm_sstore", voidType, wordType, wordType);
  module.getOrInsertFunction("evm_tload", wordType, wordType);
  module.getOrInsertFunction("evm_tstore", voidType, wordType, wordType);
  module.getOrInsertFunction("evm_balance", wordType, ptrType, wordType);
  module.getOrInsertFunction("evm_calldataload", wordType, ptrType, wordType);
  module.getOrInsertFunction("evm_calldatasize", wordType, ptrType);
  module.getOrInsertFunction("evm_calldatacopy", voidType, ptrType, ptrType,
                             wordType, wordType, wordType);
  module.getOrInsertFunction("evm_codesize", wordType, ptrType);
  module.getOrInsertFunction("evm_codecopy", voidType, ptrType, ptrType,
                             wordType, wordType, wordType);
  module.getOrInsertFunction("evm_extcodecopy", voidType, ptrType, ptrType,
                             wordType, wordType, wordType, wordType);
  module.getOrInsertFunction("evm_returndatasize", wordType, ptrType);
  module.getOrInsertFunction("evm_returndatacopy", voidType, ptrType, ptrType,
                             wordType, wordType, wordType);
  module.getOrInsertFunction("evm_sha3", wordType, ptrType, wordType, wordType);
  module.getOrInsertFunction("evm_log0", voidType, ptrType, wordType, wordType);
  module.getOrInsertFunction("evm_log1", voidType, ptrType, wordType, wordType,
                             wordType);
  module.getOrInsertFunction("evm_log2", voidType, ptrType, wordType, wordType,
                             wordType, wordType);
  module.getOrInsertFunction("evm_log3", voidType, ptrType, wordType, wordType,
                             wordType, wordType, wordType);
  module.getOrInsertFunction("evm_log4", voidType, ptrType, wordType, wordType,
                             wordType, wordType, wordType, wordType);
  module.getOrInsertFunction("evm_call", wordType, ptrType, ptrType, ptrType,
                             wordType, wordType, wordType, wordType, wordType,
                             wordType, wordType);
  module.getOrInsertFunction("evm_delegatecall", wordType, ptrType, ptrType,
                             ptrType, wordType, wordType, wordType, wordType,
                             wordType, wordType);
  module.getOrInsertFunction("evm_staticcall", wordType, ptrType, ptrType,
                             ptrType, wordType, wordType, wordType, wordType,
                             wordType, wordType);
  module.getOrInsertFunction("evm_callvalue", wordType, ptrType);
  module.getOrInsertFunction("evm_address", wordType, ptrType);
  module.getOrInsertFunction("evm_caller", wordType, ptrType);
  module.getOrInsertFunction("evm_origin", wordType, ptrType);
  module.getOrInsertFunction("evm_extcodesize", wordType, ptrType, wordType);
  module.getOrInsertFunction("evm_extcodehash", wordType, ptrType, wordType);
  module.getOrInsertFunction("evm_gasprice", wordType, ptrType);
  module.getOrInsertFunction("evm_blockhash", wordType, ptrType, wordType);
  module.getOrInsertFunction("evm_coinbase", wordType, ptrType);
  module.getOrInsertFunction("evm_timestamp", wordType, ptrType);
  module.getOrInsertFunction("evm_number", wordType, ptrType);
  module.getOrInsertFunction("evm_prevrandao", wordType, ptrType);
  module.getOrInsertFunction("evm_gaslimit", wordType, ptrType);
  module.getOrInsertFunction("evm_chainid", wordType, ptrType);
  module.getOrInsertFunction("evm_basefee", wordType, ptrType);
  module.getOrInsertFunction("evm_blobhash", wordType, ptrType, wordType);
  module.getOrInsertFunction("evm_blobbasefee", wordType, ptrType);
  module.getOrInsertFunction("evm_gas", wordType, ptrType);
  module.getOrInsertFunction("evm_pc", wordType, ptrType);
  module.getOrInsertFunction("evm_selfbalance", wordType, ptrType);
  module.getOrInsertFunction("evm_selfdestruct", voidType, ptrType, wordType);
  module.getOrInsertFunction("evm_create", wordType, ptrType, ptrType,
                             wordType, wordType, wordType);
  module.getOrInsertFunction("evm_create2", wordType, ptrType, ptrType,
                             wordType, wordType, wordType, wordType);
  module.getOrInsertFunction("evm_callcode", wordType, ptrType, ptrType,
                             ptrType, wordType, wordType, wordType, wordType,
                             wordType, wordType, wordType);
  module.getOrInsertFunction("evm_div", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_sdiv", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_mod", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_smod", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_addmod", wordType, wordType, wordType, wordType);
  module.getOrInsertFunction("evm_mulmod", wordType, wordType, wordType, wordType);
  module.getOrInsertFunction("evm_exp", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_signextend", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_byte", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_shl", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_shr", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_sar", wordType, wordType, wordType);
  module.getOrInsertFunction("evm_return", voidType, ptrType, wordType, wordType);
  module.getOrInsertFunction("evm_revert", voidType, ptrType, wordType, wordType);
}

}  // namespace notdec::evm2llvm
