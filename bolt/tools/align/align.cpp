//===- bolt/tools/align/align.cpp ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Add description.
//
//===----------------------------------------------------------------------===//

#include "bolt/Rewrite/RewriteInstance.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"

#define DEBUG_TYPE "bolt"

using namespace llvm;
using namespace object;
using namespace bolt;

namespace opts {

static cl::OptionCategory *AlignCategories[] = {
    &AlignCategory};

static cl::opt<std::string> InputFilename1(cl::Positional,
                                           cl::desc("<executable>"),
                                           cl::Required,
                                           cl::cat(AlignCategory),
                                           cl::sub(cl::SubCommand::getAll()));

static cl::opt<std::string> InputFilename2(cl::Positional,
                                           cl::desc("<executable>"),
                                           cl::Required,
                                           cl::cat(AlignCategory),
                                           cl::sub(cl::SubCommand::getAll()));

static cl::opt<std::string> OutputFilename1("o1",
                                            cl::desc("<output file>"),
                                            cl::Required,
                                            cl::cat(AlignCategory));

static cl::opt<std::string> OutputFilename2("o2",
                                            cl::desc("<output file>"),
                                            cl::Required,
                                            cl::cat(AlignCategory));

} // namespace opts

static StringRef ToolName = "llvm-bolt-align";

static void report_error(StringRef Message, std::error_code EC) {
  assert(EC);
  errs() << ToolName << ": '" << Message << "': " << EC.message() << ".\n";
  exit(1);
}

static void report_error(StringRef Message, Error E) {
  assert(E);
  errs() << ToolName << ": '" << Message << "': " << toString(std::move(E))
         << ".\n";
  exit(1);
}

void ParseCommandLine(int argc, char **argv) {
  cl::HideUnrelatedOptions(ArrayRef(opts::AlignCategories));
  // Register the target printer for --version.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  cl::ParseCommandLineOptions(argc, argv, "Align\n");
}

static std::string GetExecutablePath(const char *Argv0) {
  SmallString<256> ExecutablePath(Argv0);
  // Do a PATH lookup if Argv0 isn't a valid path.
  if (!llvm::sys::fs::exists(ExecutablePath))
    if (llvm::ErrorOr<std::string> P =
            llvm::sys::findProgramByName(ExecutablePath))
      ExecutablePath = *P;
  return std::string(ExecutablePath.str());
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);

  std::string ToolPath = GetExecutablePath(argv[0]);

  llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  // Initialize targets and assembly printers/parsers.
#define BOLT_TARGET(target)                                                    \
  LLVMInitialize##target##TargetInfo();                                        \
  LLVMInitialize##target##TargetMC();                                          \
  LLVMInitialize##target##AsmParser();                                         \
  LLVMInitialize##target##Disassembler();                                      \
  LLVMInitialize##target##Target();                                            \
  LLVMInitialize##target##AsmPrinter();

#include "bolt/Core/TargetConfig.def"

  ParseCommandLine(argc, argv);

  if (!sys::fs::exists(opts::InputFilename1))
    report_error(opts::InputFilename1, errc::no_such_file_or_directory);

  if (!sys::fs::exists(opts::InputFilename2))
    report_error(opts::InputFilename2, errc::no_such_file_or_directory);

  Expected<OwningBinary<Binary>> BinaryOrErr1 =
      createBinary(opts::InputFilename1);
  if (Error E = BinaryOrErr1.takeError())
    report_error(opts::InputFilename1, std::move(E));
  Binary &Binary1 = *BinaryOrErr1.get().getBinary();

  Expected<OwningBinary<Binary>> BinaryOrErr2 =
      createBinary(opts::InputFilename2);
  if (Error E = BinaryOrErr2.takeError())
    report_error(opts::InputFilename2, std::move(E));
  Binary &Binary2 = *BinaryOrErr2.get().getBinary();

  auto *E1 = dyn_cast<ELFObjectFileBase>(&Binary1);
  if (!E1) {
    report_error(opts::InputFilename1, object_error::invalid_file_type);
  }

  auto *E2 = dyn_cast<ELFObjectFileBase>(&Binary2);
  if (!E2) {
    report_error(opts::InputFilename2, object_error::invalid_file_type);
  }

  auto RI1OrErr = RewriteInstance::create(E1, argc, argv, ToolPath);
  if (Error E = RI1OrErr.takeError())
    report_error(opts::InputFilename1, std::move(E));
  auto &RI1 = RI1OrErr.get();

  auto RI2OrErr = RewriteInstance::create(E2, argc, argv, ToolPath);
  if (Error E = RI2OrErr.takeError())
    report_error(opts::InputFilename2, std::move(E));
  auto &RI2 = RI2OrErr.get();

  opts::DiffOnly = true;

  if (Error E = RI1->run())
    report_error(opts::InputFilename1, std::move(E));

  if (Error E = RI2->run())
    report_error(opts::InputFilename2, std::move(E));

  RI1->alignBinaries(*RI2, opts::OutputFilename1, opts::OutputFilename2);

  return EXIT_SUCCESS;
}
