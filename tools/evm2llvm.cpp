#include "notdec-evm2llvm/FactLoader.h"
#include "notdec-evm2llvm/LlvmLowerer.h"

#include <iostream>
#include <string>
#include <system_error>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace {

struct CliOptions {
  std::string FactsDir;
  std::string OutputPath;
  std::string ModuleName = "notdec.evm2llvm";
  notdec::evm2llvm::EvmMemoryModel MemoryModel =
      notdec::evm2llvm::EvmMemoryModel::IntToPtr;
};

void printUsage(const char *argv0) {
  std::cerr << "usage: " << argv0
            << " --facts <gigahorse-out-dir> --output <contract.ll> "
               "[--module-name <name>] [--memory-model inttoptr|global-array]\n";
}

bool parseMemoryModel(const std::string &text,
                      notdec::evm2llvm::EvmMemoryModel &model) {
  if (text == "inttoptr") {
    model = notdec::evm2llvm::EvmMemoryModel::IntToPtr;
    return true;
  }
  if (text == "global-array") {
    model = notdec::evm2llvm::EvmMemoryModel::GlobalArray;
    return true;
  }
  return false;
}

llvm::Expected<CliOptions> parseArgs(int argc, char **argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto readValue = [&](const char *name) -> llvm::Expected<std::string> {
      if (i + 1 >= argc) {
        return llvm::createStringError(std::errc::invalid_argument,
                                       "%s expects a value", name);
      }
      return std::string(argv[++i]);
    };

    if (arg == "--facts") {
      auto value = readValue("--facts");
      if (!value) {
        return value.takeError();
      }
      options.FactsDir = *value;
    } else if (arg == "--output" || arg == "-o") {
      auto value = readValue("--output");
      if (!value) {
        return value.takeError();
      }
      options.OutputPath = *value;
    } else if (arg == "--module-name") {
      auto value = readValue("--module-name");
      if (!value) {
        return value.takeError();
      }
      options.ModuleName = *value;
    } else if (arg == "--memory-model") {
      auto value = readValue("--memory-model");
      if (!value) {
        return value.takeError();
      }
      if (!parseMemoryModel(*value, options.MemoryModel)) {
        return llvm::createStringError(
            std::errc::invalid_argument,
            "--memory-model must be inttoptr or global-array");
      }
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    } else {
      return llvm::createStringError(std::errc::invalid_argument,
                                     "unknown argument: %s", arg.c_str());
    }
  }

  if (options.FactsDir.empty()) {
    return llvm::createStringError(std::errc::invalid_argument, "--facts is required");
  }
  if (options.OutputPath.empty()) {
    return llvm::createStringError(std::errc::invalid_argument, "--output is required");
  }
  return options;
}

int writeModule(const llvm::Module &module, const std::string &outputPath) {
  std::error_code errorCode;
  llvm::raw_fd_ostream output(outputPath, errorCode);
  if (errorCode) {
    std::cerr << "failed to open output file: " << outputPath << ": "
              << errorCode.message() << '\n';
    return 1;
  }

  module.print(output, nullptr);
  return 0;
}

void printError(llvm::Error error) {
  llvm::handleAllErrors(std::move(error), [](const llvm::ErrorInfoBase &info) {
    std::cerr << info.message() << '\n';
  });
}

}  // namespace

int main(int argc, char **argv) {
  auto optionsOrError = parseArgs(argc, argv);
  if (!optionsOrError) {
    printUsage(argv[0]);
    printError(optionsOrError.takeError());
    return 1;
  }

  auto programOrError = notdec::evm2llvm::loadFacts({optionsOrError->FactsDir});
  if (!programOrError) {
    printError(programOrError.takeError());
    return 1;
  }

  llvm::LLVMContext context;
  auto moduleOrError = notdec::evm2llvm::lowerToLlvm(
      context, *programOrError,
      {optionsOrError->ModuleName, optionsOrError->MemoryModel});
  if (!moduleOrError) {
    printError(moduleOrError.takeError());
    return 1;
  }

  if (llvm::verifyModule(**moduleOrError, &llvm::errs())) {
    std::cerr << "module verification failed\n";
    return 1;
  }

  return writeModule(**moduleOrError, optionsOrError->OutputPath);
}
