/*
    SPDX-License-Identifier: Apache-2.0

    Copyright 2023 Yingwei Zheng
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
        http://www.apache.org/licenses/LICENSE-2.0
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "FSubFuscatorPass.hpp"
#include <llvm/Bitcode/BitcodeWriterPass.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRPrinter/IRPrintingPasses.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdlib>

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"),
                                          cl::cat(FsubFuscatorCategory));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"),
                                           cl::cat(FsubFuscatorCategory));

static cl::opt<bool> OutputAssembly("S",
                                    cl::desc("Write output as LLVM assembly"),
                                    cl::cat(FsubFuscatorCategory));

int main(int argc, char **argv) {
  InitLLVM Init{argc, argv};
  setBugReportMsg(
      "PLEASE submit a bug report to https://github.com/dtcxzyw/fsubfuscator "
      "and include the crash backtrace, preprocessed "
      "source, and associated run script.\n");
  cl::ParseCommandLineOptions(argc, argv, "fsubfuscator FSub fuscator\n");

  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeScalarOpts(Registry);
  initializeVectorization(Registry);
  initializeAnalysis(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);

  LLVMContext Context;
  SMDiagnostic Err;
  auto M = parseIRFile(InputFilename, Err, Context);
  if (!M) {
    Err.print(argv[0], errs());
    return EXIT_FAILURE;
  }

  std::unique_ptr<ToolOutputFile> Out;
  // Default to standard output.
  if (OutputFilename.empty())
    OutputFilename = "-";

  std::error_code EC;
  sys::fs::OpenFlags Flags =
      OutputAssembly ? sys::fs::OF_Text : sys::fs::OF_None;
  Out.reset(new ToolOutputFile(OutputFilename, EC, Flags));
  if (EC) {
    errs() << EC.message() << '\n';
    return EXIT_FAILURE;
  }

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB;
  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  FunctionPassManager FPM;
  addFSubFuscatorPasses(FPM, OptimizationLevel::O3);
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  if (OutputAssembly)
    MPM.addPass(PrintModulePass(Out->os(), ""));
  else
    MPM.addPass(BitcodeWriterPass(Out->os()));
  MPM.run(*M, MAM);
  Out->keep();

  return EXIT_SUCCESS;
}
