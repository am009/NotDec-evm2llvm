#include "notdec-evm2llvm/InstructionLowerer.h"

#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

namespace notdec::evm2llvm {
namespace {

llvm::Error unsupported(const TacStatement &stmt) {
  return llvm::createStringError(std::errc::not_supported,
                                 "unsupported opcode %s at %s", stmt.Op.c_str(),
                                 stmt.Id.c_str());
}

bool isPushOpcode(llvm::StringRef op) {
  if (op == "PUSH") {
    return true;
  }
  if (!op.consume_front("PUSH")) {
    return false;
  }
  unsigned width = 0;
  return !op.getAsInteger(10, width) && width <= 32;
}

bool isDupOrSwapOpcode(llvm::StringRef op) {
  if (op == "DUP" || op == "SWAP") {
    return true;
  }
  if (op.consume_front("DUP")) {
    unsigned index = 0;
    return !op.getAsInteger(10, index) && index >= 1 && index <= 16;
  }
  if (op.consume_front("SWAP")) {
    unsigned index = 0;
    return !op.getAsInteger(10, index) && index >= 1 && index <= 16;
  }
  return false;
}

}  // namespace

InstructionLowerer::InstructionLowerer(
    llvm::IRBuilder<> &builder, llvm::LLVMContext &context, llvm::Type *wordType,
    std::map<FactId, llvm::Value *> &values, const TacProgram &program,
    RuntimeHandles handles, EvmMemoryModel memoryModel)
    : Builder(builder),
      Context(context),
      WordType(wordType),
      Values(values),
      Program(program),
      Handles(handles),
      MemoryModel(memoryModel) {}

llvm::APInt InstructionLowerer::parseWordConstant(const std::string &text) const {
  llvm::StringRef value(text);
  unsigned radix = 10;
  if (value.consume_front("0x") || value.consume_front("0X")) {
    radix = 16;
  }
  return llvm::APInt(256, value, radix);
}

llvm::Expected<llvm::Value *> InstructionLowerer::loadWord(const FactId &var) {
  auto value = Values.find(var);
  if (value != Values.end()) {
    return value->second;
  }

  auto constant = Program.VariableValues.find(var);
  if (constant != Program.VariableValues.end()) {
    return llvm::ConstantInt::get(Context, parseWordConstant(constant->second));
  }

  return llvm::createStringError(std::errc::invalid_argument,
                                 "missing SSA value for variable %s", var.c_str());
}

llvm::Error InstructionLowerer::defineWord(const FactId &var, llvm::Value *value) {
  if (!Values.emplace(var, value).second) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "duplicate SSA value definition for variable %s",
                                   var.c_str());
  }
  return llvm::Error::success();
}

llvm::Value *InstructionLowerer::boolToWord(llvm::Value *value) {
  return Builder.CreateZExt(value, WordType, "evm.bool");
}

llvm::GlobalVariable *InstructionLowerer::memoryGlobal() {
  if (Memory != nullptr) {
    return Memory;
  }

  auto *module = Builder.GetInsertBlock()->getModule();
  auto *byteType = llvm::Type::getInt8Ty(Context);
  auto *arrayType = llvm::ArrayType::get(byteType, 1024 * 1024);
  std::string name = "notdec_evm_memory";
  unsigned index = 1;
  while (llvm::Value *existing = module->getNamedValue(name)) {
    auto *global = llvm::dyn_cast<llvm::GlobalVariable>(existing);
    if (global != nullptr && global->getValueType() == arrayType) {
      Memory = global;
      return Memory;
    }
    name = "notdec_evm_memory." + std::to_string(index++);
  }

  Memory = new llvm::GlobalVariable(*module, arrayType, false,
                                    llvm::GlobalValue::ExternalLinkage,
                                    nullptr, name);
  return Memory;
}

llvm::Value *InstructionLowerer::memoryPointer(llvm::Value *address) {
  if (MemoryModel == EvmMemoryModel::IntToPtr) {
    return Builder.CreateIntToPtr(address, llvm::PointerType::get(Context, 0),
                                  "evm.mem.ptr");
  }

  llvm::Value *zero = llvm::ConstantInt::get(address->getType(), 0);
  return Builder.CreateGEP(memoryGlobal()->getValueType(), memoryGlobal(),
                           {zero, address}, "evm.mem.ptr");
}

llvm::Function *InstructionLowerer::runtimeFunction(const char *name) {
  auto *function = Builder.GetInsertBlock()->getParent();
  return function->getParent()->getFunction(name);
}

llvm::Expected<std::vector<llvm::Value *>> InstructionLowerer::loadOperands(
    const TacStatement &stmt, unsigned expectedCount) {
  if (stmt.Uses.size() != expectedCount) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "%s expects %u operands at %s",
                                   stmt.Op.c_str(), expectedCount,
                                   stmt.Id.c_str());
  }

  std::vector<llvm::Value *> operands;
  operands.reserve(stmt.Uses.size());
  for (const auto &use : stmt.Uses) {
    auto valueOrError = loadWord(use);
    if (!valueOrError) {
      return valueOrError.takeError();
    }
    operands.push_back(*valueOrError);
  }
  return operands;
}

llvm::Expected<llvm::Value *> InstructionLowerer::lowerUnary(
    const TacStatement &stmt) {
  if (stmt.Uses.size() != 1) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "%s expects one operand at %s", stmt.Op.c_str(),
                                   stmt.Id.c_str());
  }

  auto inputOrError = loadWord(stmt.Uses[0]);
  if (!inputOrError) {
    return inputOrError.takeError();
  }
  auto *input = *inputOrError;

  if (stmt.Op == "MOV") {
    return input;
  }
  if (stmt.Op == "NOT") {
    return Builder.CreateXor(input, llvm::ConstantInt::get(Context, llvm::APInt::getAllOnes(256)),
                             "evm.not");
  }
  if (stmt.Op == "ISZERO") {
    auto *zero = llvm::ConstantInt::get(WordType, 0);
    return boolToWord(Builder.CreateICmpEQ(input, zero, "evm.iszero"));
  }

  return unsupported(stmt);
}

llvm::Expected<llvm::Value *> InstructionLowerer::lowerBinary(
    const TacStatement &stmt) {
  if (stmt.Uses.size() != 2) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "%s expects two operands at %s", stmt.Op.c_str(),
                                   stmt.Id.c_str());
  }

  auto lhsOrError = loadWord(stmt.Uses[0]);
  if (!lhsOrError) {
    return lhsOrError.takeError();
  }
  auto rhsOrError = loadWord(stmt.Uses[1]);
  if (!rhsOrError) {
    return rhsOrError.takeError();
  }

  auto *lhs = *lhsOrError;
  auto *rhs = *rhsOrError;

  if (stmt.Op == "ADD") {
    return Builder.CreateAdd(lhs, rhs, "evm.add");
  }
  if (stmt.Op == "SUB") {
    return Builder.CreateSub(lhs, rhs, "evm.sub");
  }
  if (stmt.Op == "MUL") {
    return Builder.CreateMul(lhs, rhs, "evm.mul");
  }
  if (stmt.Op == "AND") {
    return Builder.CreateAnd(lhs, rhs, "evm.and");
  }
  if (stmt.Op == "OR") {
    return Builder.CreateOr(lhs, rhs, "evm.or");
  }
  if (stmt.Op == "XOR") {
    return Builder.CreateXor(lhs, rhs, "evm.xor");
  }
  if (stmt.Op == "EQ") {
    return boolToWord(Builder.CreateICmpEQ(lhs, rhs, "evm.eq"));
  }
  if (stmt.Op == "LT") {
    return boolToWord(Builder.CreateICmpULT(lhs, rhs, "evm.lt"));
  }
  if (stmt.Op == "GT") {
    return boolToWord(Builder.CreateICmpUGT(lhs, rhs, "evm.gt"));
  }
  if (stmt.Op == "SLT") {
    return boolToWord(Builder.CreateICmpSLT(lhs, rhs, "evm.slt"));
  }
  if (stmt.Op == "SGT") {
    return boolToWord(Builder.CreateICmpSGT(lhs, rhs, "evm.sgt"));
  }
  if (stmt.Op == "DIV") {
    return Builder.CreateCall(runtimeFunction("evm_div"), {lhs, rhs}, "evm.div");
  }
  if (stmt.Op == "SDIV") {
    return Builder.CreateCall(runtimeFunction("evm_sdiv"), {lhs, rhs}, "evm.sdiv");
  }
  if (stmt.Op == "MOD") {
    return Builder.CreateCall(runtimeFunction("evm_mod"), {lhs, rhs}, "evm.mod");
  }
  if (stmt.Op == "SMOD") {
    return Builder.CreateCall(runtimeFunction("evm_smod"), {lhs, rhs}, "evm.smod");
  }
  if (stmt.Op == "EXP") {
    return Builder.CreateCall(runtimeFunction("evm_exp"), {lhs, rhs}, "evm.exp");
  }
  if (stmt.Op == "SIGNEXTEND") {
    return Builder.CreateCall(runtimeFunction("evm_signextend"), {lhs, rhs},
                              "evm.signextend");
  }
  if (stmt.Op == "BYTE") {
    return Builder.CreateCall(runtimeFunction("evm_byte"), {lhs, rhs}, "evm.byte");
  }
  if (stmt.Op == "SHL") {
    return Builder.CreateCall(runtimeFunction("evm_shl"), {lhs, rhs}, "evm.shl");
  }
  if (stmt.Op == "SHR") {
    return Builder.CreateCall(runtimeFunction("evm_shr"), {lhs, rhs}, "evm.shr");
  }
  if (stmt.Op == "SAR") {
    return Builder.CreateCall(runtimeFunction("evm_sar"), {lhs, rhs}, "evm.sar");
  }
  if (stmt.Op == "SHA3") {
    return Builder.CreateCall(runtimeFunction("evm_sha3"), {Handles.Mem, lhs, rhs},
                              "evm.sha3");
  }
  if (stmt.Op == "KECCAK256") {
    return Builder.CreateCall(runtimeFunction("evm_sha3"), {Handles.Mem, lhs, rhs},
                              "evm.keccak256");
  }

  return unsupported(stmt);
}

llvm::Expected<llvm::Value *> InstructionLowerer::lowerTernary(
    const TacStatement &stmt) {
  if (stmt.Uses.size() != 3) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "%s expects three operands at %s", stmt.Op.c_str(),
                                   stmt.Id.c_str());
  }

  std::vector<llvm::Value *> operands;
  operands.reserve(stmt.Uses.size());
  for (const auto &use : stmt.Uses) {
    auto valueOrError = loadWord(use);
    if (!valueOrError) {
      return valueOrError.takeError();
    }
    operands.push_back(*valueOrError);
  }

  if (stmt.Op == "ADDMOD") {
    return Builder.CreateCall(runtimeFunction("evm_addmod"), operands,
                              "evm.addmod");
  }
  if (stmt.Op == "MULMOD") {
    return Builder.CreateCall(runtimeFunction("evm_mulmod"), operands,
                              "evm.mulmod");
  }

  return unsupported(stmt);
}

llvm::Expected<llvm::Value *> InstructionLowerer::lowerStateRead(
    const TacStatement &stmt) {
  if (stmt.Op == "CALLDATASIZE") {
    if (!stmt.Uses.empty()) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "CALLDATASIZE expects no operands at %s",
                                     stmt.Id.c_str());
    }
    return Builder.CreateCall(runtimeFunction("evm_calldatasize"),
                              {Handles.Calldata}, "evm.calldatasize");
  }
  if (stmt.Op == "CODESIZE") {
    if (!stmt.Uses.empty()) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "CODESIZE expects no operands at %s",
                                     stmt.Id.c_str());
    }
    return Builder.CreateCall(runtimeFunction("evm_codesize"), {Handles.Env},
                              "evm.codesize");
  }
  if (stmt.Op == "RETURNDATASIZE") {
    if (!stmt.Uses.empty()) {
      return llvm::createStringError(
          std::errc::invalid_argument,
          "RETURNDATASIZE expects no operands at %s", stmt.Id.c_str());
    }
    return Builder.CreateCall(runtimeFunction("evm_returndatasize"),
                              {Handles.Returndata}, "evm.returndatasize");
  }
  if (stmt.Op == "CALLVALUE" || stmt.Op == "ADDRESS" || stmt.Op == "CALLER" ||
      stmt.Op == "ORIGIN" || stmt.Op == "GASPRICE" ||
      stmt.Op == "COINBASE" || stmt.Op == "TIMESTAMP" ||
      stmt.Op == "NUMBER" || stmt.Op == "DIFFICULTY" ||
      stmt.Op == "PREVRANDAO" || stmt.Op == "GASLIMIT" ||
      stmt.Op == "CHAINID" || stmt.Op == "BASEFEE" ||
      stmt.Op == "BLOBBASEFEE" || stmt.Op == "GAS" ||
      stmt.Op == "PC" || stmt.Op == "MSIZE" || stmt.Op == "SELFBALANCE") {
    if (!stmt.Uses.empty()) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "%s expects no operands at %s", stmt.Op.c_str(),
                                     stmt.Id.c_str());
    }
    if (stmt.Op == "CALLVALUE") {
      return Builder.CreateCall(runtimeFunction("evm_callvalue"), {Handles.Env},
                                "evm.callvalue");
    }
    if (stmt.Op == "ADDRESS") {
      return Builder.CreateCall(runtimeFunction("evm_address"), {Handles.Env},
                                "evm.address");
    }
    if (stmt.Op == "CALLER") {
      return Builder.CreateCall(runtimeFunction("evm_caller"), {Handles.Env},
                                "evm.caller");
    }
    if (stmt.Op == "ORIGIN") {
      return Builder.CreateCall(runtimeFunction("evm_origin"), {Handles.Env},
                                "evm.origin");
    }
    if (stmt.Op == "GASPRICE") {
      return Builder.CreateCall(runtimeFunction("evm_gasprice"), {Handles.Env},
                                "evm.gasprice");
    }
    if (stmt.Op == "COINBASE") {
      return Builder.CreateCall(runtimeFunction("evm_coinbase"), {Handles.Env},
                                "evm.coinbase");
    }
    if (stmt.Op == "TIMESTAMP") {
      return Builder.CreateCall(runtimeFunction("evm_timestamp"), {Handles.Env},
                                "evm.timestamp");
    }
    if (stmt.Op == "NUMBER") {
      return Builder.CreateCall(runtimeFunction("evm_number"), {Handles.Env},
                                "evm.number");
    }
    if (stmt.Op == "DIFFICULTY" || stmt.Op == "PREVRANDAO") {
      return Builder.CreateCall(runtimeFunction("evm_prevrandao"), {Handles.Env},
                                "evm.prevrandao");
    }
    if (stmt.Op == "GASLIMIT") {
      return Builder.CreateCall(runtimeFunction("evm_gaslimit"), {Handles.Env},
                                "evm.gaslimit");
    }
    if (stmt.Op == "CHAINID") {
      return Builder.CreateCall(runtimeFunction("evm_chainid"), {Handles.Env},
                                "evm.chainid");
    }
    if (stmt.Op == "BASEFEE") {
      return Builder.CreateCall(runtimeFunction("evm_basefee"), {Handles.Env},
                                "evm.basefee");
    }
    if (stmt.Op == "BLOBBASEFEE") {
      return Builder.CreateCall(runtimeFunction("evm_blobbasefee"), {Handles.Env},
                                "evm.blobbasefee");
    }
    if (stmt.Op == "GAS") {
      return Builder.CreateCall(runtimeFunction("evm_gas"), {Handles.Env},
                                "evm.gas");
    }
    if (stmt.Op == "PC") {
      return Builder.CreateCall(runtimeFunction("evm_pc"), {Handles.Env},
                                "evm.pc");
    }
    if (stmt.Op == "SELFBALANCE") {
      return Builder.CreateCall(runtimeFunction("evm_selfbalance"), {Handles.Env},
                                "evm.selfbalance");
    }
    return Builder.CreateCall(runtimeFunction("evm_msize"), {Handles.Mem},
                              "evm.msize");
  }

  if (stmt.Uses.size() != 1) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "%s expects one operand at %s", stmt.Op.c_str(),
                                   stmt.Id.c_str());
  }

  auto operandOrError = loadWord(stmt.Uses[0]);
  if (!operandOrError) {
    return operandOrError.takeError();
  }
  auto *operand = *operandOrError;

  if (stmt.Op == "MLOAD") {
    auto *load = Builder.CreateLoad(WordType, memoryPointer(operand), "evm.mload");
    load->setAlignment(llvm::Align(1));
    return load;
  }
  if (stmt.Op == "SLOAD") {
    return Builder.CreateCall(runtimeFunction("evm_sload"), {operand},
                              "evm.sload");
  }
  if (stmt.Op == "TLOAD") {
    return Builder.CreateCall(runtimeFunction("evm_tload"), {operand},
                              "evm.tload");
  }
  if (stmt.Op == "BALANCE") {
    return Builder.CreateCall(runtimeFunction("evm_balance"),
                              {Handles.Env, operand}, "evm.balance");
  }
  if (stmt.Op == "BLOCKHASH") {
    return Builder.CreateCall(runtimeFunction("evm_blockhash"),
                              {Handles.Env, operand}, "evm.blockhash");
  }
  if (stmt.Op == "BLOBHASH") {
    return Builder.CreateCall(runtimeFunction("evm_blobhash"),
                              {Handles.Env, operand}, "evm.blobhash");
  }
  if (stmt.Op == "CALLDATALOAD") {
    return Builder.CreateCall(runtimeFunction("evm_calldataload"),
                              {Handles.Calldata, operand}, "evm.calldataload");
  }
  if (stmt.Op == "EXTCODESIZE") {
    return Builder.CreateCall(runtimeFunction("evm_extcodesize"),
                              {Handles.Env, operand}, "evm.extcodesize");
  }
  if (stmt.Op == "EXTCODEHASH") {
    return Builder.CreateCall(runtimeFunction("evm_extcodehash"),
                              {Handles.Env, operand}, "evm.extcodehash");
  }

  return unsupported(stmt);
}

llvm::Error InstructionLowerer::lowerStateWrite(const TacStatement &stmt) {
  if (stmt.Op == "CALL") {
    if (stmt.Uses.size() != 7 || stmt.Defs.size() != 1) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "CALL expects seven operands and one def at %s",
                                     stmt.Id.c_str());
    }
    std::vector<llvm::Value *> args = {Handles.Mem, Handles.Returndata, Handles.Env};
    for (const auto &use : stmt.Uses) {
      auto valueOrError = loadWord(use);
      if (!valueOrError) {
        return valueOrError.takeError();
      }
      args.push_back(*valueOrError);
    }
    auto *success = Builder.CreateCall(runtimeFunction("evm_call"), args, "evm.call");
    return defineWord(stmt.Defs[0], success);
  }
  if (stmt.Op == "CALLCODE") {
    if (stmt.Uses.size() != 7 || stmt.Defs.size() != 1) {
      return llvm::createStringError(
          std::errc::invalid_argument,
          "CALLCODE expects seven operands and one def at %s", stmt.Id.c_str());
    }
    std::vector<llvm::Value *> args = {Handles.Mem, Handles.Returndata, Handles.Env};
    for (const auto &use : stmt.Uses) {
      auto valueOrError = loadWord(use);
      if (!valueOrError) {
        return valueOrError.takeError();
      }
      args.push_back(*valueOrError);
    }
    auto *success = Builder.CreateCall(runtimeFunction("evm_callcode"), args,
                                       "evm.callcode");
    return defineWord(stmt.Defs[0], success);
  }
  if (stmt.Op == "DELEGATECALL") {
    if (stmt.Uses.size() != 6 || stmt.Defs.size() != 1) {
      return llvm::createStringError(
          std::errc::invalid_argument,
          "DELEGATECALL expects six operands and one def at %s",
          stmt.Id.c_str());
    }
    // DELEGATECALL has CALL-like memory/returndata effects but no value
    // operand. The helper keeps those effects explicit for later passes.
    std::vector<llvm::Value *> args = {Handles.Mem, Handles.Returndata,
                                       Handles.Env};
    for (const auto &use : stmt.Uses) {
      auto valueOrError = loadWord(use);
      if (!valueOrError) {
        return valueOrError.takeError();
      }
      args.push_back(*valueOrError);
    }
    auto *success = Builder.CreateCall(runtimeFunction("evm_delegatecall"), args,
                                       "evm.delegatecall");
    return defineWord(stmt.Defs[0], success);
  }
  if (stmt.Op == "STATICCALL") {
    if (stmt.Uses.size() != 6 || stmt.Defs.size() != 1) {
      return llvm::createStringError(
          std::errc::invalid_argument,
          "STATICCALL expects six operands and one def at %s", stmt.Id.c_str());
    }
    std::vector<llvm::Value *> args = {Handles.Mem, Handles.Returndata,
                                       Handles.Env};
    for (const auto &use : stmt.Uses) {
      auto valueOrError = loadWord(use);
      if (!valueOrError) {
        return valueOrError.takeError();
      }
      args.push_back(*valueOrError);
    }
    auto *success = Builder.CreateCall(runtimeFunction("evm_staticcall"), args,
                                       "evm.staticcall");
    return defineWord(stmt.Defs[0], success);
  }
  if (stmt.Op == "CREATE2") {
    if (stmt.Uses.size() != 4 || stmt.Defs.size() != 1) {
      return llvm::createStringError(
          std::errc::invalid_argument,
          "CREATE2 expects four operands and one def at %s", stmt.Id.c_str());
    }
    // CREATE2 reads init code from memory and returns the created address.
    // The helper also keeps account/state effects visible to later passes.
    std::vector<llvm::Value *> args = {Handles.Mem, Handles.Env};
    for (const auto &use : stmt.Uses) {
      auto valueOrError = loadWord(use);
      if (!valueOrError) {
        return valueOrError.takeError();
      }
      args.push_back(*valueOrError);
    }
    auto *address = Builder.CreateCall(runtimeFunction("evm_create2"), args,
                                       "evm.create2");
    return defineWord(stmt.Defs[0], address);
  }
  if (stmt.Op == "CREATE") {
    if (stmt.Uses.size() != 3 || stmt.Defs.size() != 1) {
      return llvm::createStringError(
          std::errc::invalid_argument,
          "CREATE expects three operands and one def at %s", stmt.Id.c_str());
    }
    std::vector<llvm::Value *> args = {Handles.Mem, Handles.Env};
    for (const auto &use : stmt.Uses) {
      auto valueOrError = loadWord(use);
      if (!valueOrError) {
        return valueOrError.takeError();
      }
      args.push_back(*valueOrError);
    }
    auto *address =
        Builder.CreateCall(runtimeFunction("evm_create"), args, "evm.create");
    return defineWord(stmt.Defs[0], address);
  }

  if (stmt.Op == "LOG0" || stmt.Op == "LOG1" || stmt.Op == "LOG2" ||
      stmt.Op == "LOG3" || stmt.Op == "LOG4") {
    unsigned topicCount = static_cast<unsigned>(stmt.Op.back() - '0');
    if (stmt.Uses.size() != topicCount + 2) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "%s expects %u operands at %s",
                                     stmt.Op.c_str(), topicCount + 2,
                                     stmt.Id.c_str());
    }

    std::vector<llvm::Value *> args = {Handles.Mem};
    for (const auto &use : stmt.Uses) {
      auto valueOrError = loadWord(use);
      if (!valueOrError) {
        return valueOrError.takeError();
      }
      args.push_back(*valueOrError);
    }
    Builder.CreateCall(runtimeFunction(("evm_log" + std::to_string(topicCount)).c_str()),
                       args);
    return llvm::Error::success();
  }

  if (stmt.Op == "SELFDESTRUCT") {
    if (stmt.Uses.size() != 1) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "SELFDESTRUCT expects one operand at %s",
                                     stmt.Id.c_str());
    }
    auto beneficiaryOrError = loadWord(stmt.Uses[0]);
    if (!beneficiaryOrError) {
      return beneficiaryOrError.takeError();
    }
    Builder.CreateCall(runtimeFunction("evm_selfdestruct"),
                       {Handles.Env, *beneficiaryOrError});
    return llvm::Error::success();
  }

  if (stmt.Op == "MCOPY") {
    if (stmt.Uses.size() != 3) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "MCOPY expects three operands at %s",
                                     stmt.Id.c_str());
    }
    auto dstOrError = loadWord(stmt.Uses[0]);
    if (!dstOrError) {
      return dstOrError.takeError();
    }
    auto srcOrError = loadWord(stmt.Uses[1]);
    if (!srcOrError) {
      return srcOrError.takeError();
    }
    auto sizeOrError = loadWord(stmt.Uses[2]);
    if (!sizeOrError) {
      return sizeOrError.takeError();
    }
    Builder.CreateCall(runtimeFunction("evm_mcopy"),
                       {Handles.Mem, *dstOrError, *srcOrError, *sizeOrError});
    return llvm::Error::success();
  }

  if (stmt.Op == "EXTCODECOPY") {
    if (stmt.Uses.size() != 4) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "EXTCODECOPY expects four operands at %s",
                                     stmt.Id.c_str());
    }
    auto addressOrError = loadWord(stmt.Uses[0]);
    if (!addressOrError) {
      return addressOrError.takeError();
    }
    auto dstOrError = loadWord(stmt.Uses[1]);
    if (!dstOrError) {
      return dstOrError.takeError();
    }
    auto srcOrError = loadWord(stmt.Uses[2]);
    if (!srcOrError) {
      return srcOrError.takeError();
    }
    auto sizeOrError = loadWord(stmt.Uses[3]);
    if (!sizeOrError) {
      return sizeOrError.takeError();
    }
    Builder.CreateCall(runtimeFunction("evm_extcodecopy"),
                       {Handles.Mem, Handles.Env, *addressOrError, *dstOrError,
                        *srcOrError, *sizeOrError});
    return llvm::Error::success();
  }

  if (stmt.Op == "CALLDATACOPY") {
    if (stmt.Uses.size() != 3) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "CALLDATACOPY expects three operands at %s",
                                     stmt.Id.c_str());
    }
    auto dstOrError = loadWord(stmt.Uses[0]);
    if (!dstOrError) {
      return dstOrError.takeError();
    }
    auto srcOrError = loadWord(stmt.Uses[1]);
    if (!srcOrError) {
      return srcOrError.takeError();
    }
    auto sizeOrError = loadWord(stmt.Uses[2]);
    if (!sizeOrError) {
      return sizeOrError.takeError();
    }
    Builder.CreateCall(runtimeFunction("evm_calldatacopy"),
                       {Handles.Mem, Handles.Calldata, *dstOrError, *srcOrError,
                        *sizeOrError});
    return llvm::Error::success();
  }
  if (stmt.Op == "CODECOPY") {
    if (stmt.Uses.size() != 3) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "CODECOPY expects three operands at %s",
                                     stmt.Id.c_str());
    }
    auto dstOrError = loadWord(stmt.Uses[0]);
    if (!dstOrError) {
      return dstOrError.takeError();
    }
    auto srcOrError = loadWord(stmt.Uses[1]);
    if (!srcOrError) {
      return srcOrError.takeError();
    }
    auto sizeOrError = loadWord(stmt.Uses[2]);
    if (!sizeOrError) {
      return sizeOrError.takeError();
    }
    Builder.CreateCall(runtimeFunction("evm_codecopy"),
                       {Handles.Mem, Handles.Env, *dstOrError, *srcOrError,
                        *sizeOrError});
    return llvm::Error::success();
  }
  if (stmt.Op == "RETURNDATACOPY") {
    if (stmt.Uses.size() != 3) {
      return llvm::createStringError(
          std::errc::invalid_argument,
          "RETURNDATACOPY expects three operands at %s", stmt.Id.c_str());
    }
    auto dstOrError = loadWord(stmt.Uses[0]);
    if (!dstOrError) {
      return dstOrError.takeError();
    }
    auto srcOrError = loadWord(stmt.Uses[1]);
    if (!srcOrError) {
      return srcOrError.takeError();
    }
    auto sizeOrError = loadWord(stmt.Uses[2]);
    if (!sizeOrError) {
      return sizeOrError.takeError();
    }
    Builder.CreateCall(runtimeFunction("evm_returndatacopy"),
                       {Handles.Mem, Handles.Returndata, *dstOrError, *srcOrError,
                        *sizeOrError});
    return llvm::Error::success();
  }

  if (stmt.Uses.size() != 2) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "%s expects two operands at %s", stmt.Op.c_str(),
                                   stmt.Id.c_str());
  }

  auto lhsOrError = loadWord(stmt.Uses[0]);
  if (!lhsOrError) {
    return lhsOrError.takeError();
  }
  auto rhsOrError = loadWord(stmt.Uses[1]);
  if (!rhsOrError) {
    return rhsOrError.takeError();
  }

  auto *lhs = *lhsOrError;
  auto *rhs = *rhsOrError;

  if (stmt.Op == "MSTORE") {
    auto *store = Builder.CreateStore(rhs, memoryPointer(lhs));
    store->setAlignment(llvm::Align(1));
    return llvm::Error::success();
  }
  if (stmt.Op == "MSTORE8") {
    auto *byteValue = Builder.CreateTrunc(rhs, llvm::Type::getInt8Ty(Context),
                                          "evm.mstore8.byte");
    auto *store = Builder.CreateStore(byteValue, memoryPointer(lhs));
    store->setAlignment(llvm::Align(1));
    return llvm::Error::success();
  }
  if (stmt.Op == "SSTORE") {
    Builder.CreateCall(runtimeFunction("evm_sstore"), {lhs, rhs});
    return llvm::Error::success();
  }
  if (stmt.Op == "TSTORE") {
    Builder.CreateCall(runtimeFunction("evm_tstore"), {lhs, rhs});
    return llvm::Error::success();
  }
  if (stmt.Op == "RETURN") {
    Builder.CreateCall(runtimeFunction("evm_return"), {Handles.Mem, lhs, rhs});
    return llvm::Error::success();
  }
  if (stmt.Op == "REVERT") {
    Builder.CreateCall(runtimeFunction("evm_revert"), {Handles.Mem, lhs, rhs});
    return llvm::Error::success();
  }

  return unsupported(stmt);
}

llvm::Error InstructionLowerer::lower(const TacStatement &stmt) {
  if (stmt.Op == "JUMP" || stmt.Op == "JUMPI" || stmt.Op == "STOP" ||
      stmt.Op == "THROW" || stmt.Op == "INVALID" || stmt.Op == "JUMPDEST" ||
      stmt.Op == "RETURNPRIVATE") {
    return llvm::Error::success();
  }
  if (stmt.Op == "POP") {
    if (!stmt.Defs.empty()) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "POP expects no defs at %s", stmt.Id.c_str());
    }
    return llvm::Error::success();
  }
  if (isDupOrSwapOpcode(stmt.Op)) {
    if (stmt.Defs.size() != stmt.Uses.size()) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "%s expects matching use/def counts at %s",
                                     stmt.Op.c_str(), stmt.Id.c_str());
    }
    auto valuesOrError = loadOperands(stmt, stmt.Uses.size());
    if (!valuesOrError) {
      return valuesOrError.takeError();
    }
    for (unsigned i = 0; i < stmt.Defs.size(); ++i) {
      if (auto error = defineWord(stmt.Defs[i], (*valuesOrError)[i])) {
        return error;
      }
    }
    return llvm::Error::success();
  }
  if (stmt.Op == "CALLPRIVATE") {
    auto *zero = llvm::ConstantInt::get(WordType, 0);
    for (const auto &def : stmt.Defs) {
      if (auto error = defineWord(def, zero)) {
        return error;
      }
    }
    return llvm::Error::success();
  }
  if (stmt.Op == "PHI") {
    if (stmt.Defs.size() != 1) {
      return llvm::createStringError(std::errc::not_supported,
                                     "PHI expects one def at %s", stmt.Id.c_str());
    }
    if (stmt.Uses.empty()) {
      return llvm::Error::success();
    }
    auto valueOrError = loadWord(stmt.Uses[0]);
    if (!valueOrError) {
      return valueOrError.takeError();
    }
    return defineWord(stmt.Defs[0], *valueOrError);
  }
  if (stmt.Op == "MSTORE" || stmt.Op == "MSTORE8" || stmt.Op == "SSTORE" ||
      stmt.Op == "TSTORE" ||
      stmt.Op == "RETURN" || stmt.Op == "REVERT" ||
      stmt.Op == "MCOPY" || stmt.Op == "EXTCODECOPY" ||
      stmt.Op == "CALLDATACOPY" || stmt.Op == "CODECOPY" ||
      stmt.Op == "RETURNDATACOPY" ||
      stmt.Op == "LOG0" || stmt.Op == "LOG1" || stmt.Op == "LOG2" ||
      stmt.Op == "LOG3" || stmt.Op == "LOG4" || stmt.Op == "CALL" ||
      stmt.Op == "CALLCODE" || stmt.Op == "DELEGATECALL" ||
      stmt.Op == "STATICCALL" || stmt.Op == "CREATE" || stmt.Op == "CREATE2" ||
      stmt.Op == "SELFDESTRUCT") {
    return lowerStateWrite(stmt);
  }

  llvm::Value *value = nullptr;

  if (stmt.Op == "CONST" || isPushOpcode(stmt.Op)) {
    if (stmt.Defs.size() != 1) {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "%s expects one def at %s", stmt.Op.c_str(),
                                     stmt.Id.c_str());
    }
    auto constant = Program.VariableValues.find(stmt.Defs[0]);
    if (stmt.Op == "PUSH0") {
      value = llvm::ConstantInt::get(WordType, 0);
    } else if (constant != Program.VariableValues.end()) {
      value = llvm::ConstantInt::get(Context, parseWordConstant(constant->second));
    } else {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "%s def %s has no TAC_Variable_Value",
                                     stmt.Op.c_str(), stmt.Defs[0].c_str());
    }
  } else if (stmt.Op == "MOV" || stmt.Op == "NOT" || stmt.Op == "ISZERO") {
    auto valueOrError = lowerUnary(stmt);
    if (!valueOrError) {
      return valueOrError.takeError();
    }
    value = *valueOrError;
  } else if (stmt.Op == "ADD" || stmt.Op == "SUB" || stmt.Op == "MUL" ||
             stmt.Op == "AND" || stmt.Op == "OR" || stmt.Op == "XOR" ||
             stmt.Op == "EQ" || stmt.Op == "LT" || stmt.Op == "GT" ||
             stmt.Op == "SLT" || stmt.Op == "SGT" || stmt.Op == "DIV" ||
             stmt.Op == "SDIV" || stmt.Op == "MOD" || stmt.Op == "SMOD" ||
             stmt.Op == "EXP" || stmt.Op == "SIGNEXTEND" || stmt.Op == "BYTE" ||
             stmt.Op == "SHL" || stmt.Op == "SHR" || stmt.Op == "SAR" ||
             stmt.Op == "SHA3" || stmt.Op == "KECCAK256") {
    auto valueOrError = lowerBinary(stmt);
    if (!valueOrError) {
      return valueOrError.takeError();
    }
    value = *valueOrError;
  } else if (stmt.Op == "ADDMOD" || stmt.Op == "MULMOD") {
    auto valueOrError = lowerTernary(stmt);
    if (!valueOrError) {
      return valueOrError.takeError();
    }
    value = *valueOrError;
  } else if (stmt.Op == "MLOAD" || stmt.Op == "SLOAD" ||
             stmt.Op == "TLOAD" ||
             stmt.Op == "BALANCE" || stmt.Op == "BLOCKHASH" ||
             stmt.Op == "CALLDATALOAD" || stmt.Op == "CALLDATASIZE" ||
             stmt.Op == "CODESIZE" ||
             stmt.Op == "RETURNDATASIZE" ||
             stmt.Op == "CALLVALUE" || stmt.Op == "ADDRESS" ||
             stmt.Op == "CALLER" || stmt.Op == "ORIGIN" ||
             stmt.Op == "GASPRICE" || stmt.Op == "EXTCODESIZE" ||
             stmt.Op == "EXTCODEHASH" || stmt.Op == "COINBASE" ||
             stmt.Op == "TIMESTAMP" || stmt.Op == "NUMBER" ||
             stmt.Op == "DIFFICULTY" || stmt.Op == "PREVRANDAO" ||
             stmt.Op == "GASLIMIT" || stmt.Op == "CHAINID" ||
             stmt.Op == "BASEFEE" || stmt.Op == "BLOBHASH" ||
             stmt.Op == "BLOBBASEFEE" || stmt.Op == "GAS" ||
             stmt.Op == "PC" || stmt.Op == "MSIZE" ||
             stmt.Op == "SELFBALANCE") {
    auto valueOrError = lowerStateRead(stmt);
    if (!valueOrError) {
      return valueOrError.takeError();
    }
    value = *valueOrError;
  } else {
    return unsupported(stmt);
  }

  if (stmt.Defs.empty()) {
    return llvm::Error::success();
  }
  if (stmt.Defs.size() != 1) {
    return llvm::createStringError(std::errc::not_supported,
                                   "%s has multiple defs at %s", stmt.Op.c_str(),
                                   stmt.Id.c_str());
  }
  return defineWord(stmt.Defs[0], value);
}

}  // namespace notdec::evm2llvm
