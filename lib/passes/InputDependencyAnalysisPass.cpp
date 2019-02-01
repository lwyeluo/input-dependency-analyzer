#include "passes/InputDependencyAnalysisPass.h"

#include "PDG/GraphBuilder.h"
#include "PDG/LLVMNode.h"
#include "analysis/InputDependencyAnalysis.h"
#include "analysis/InputDepConfig.h"
#include "passes/GraphBuilderPass.h"

#include "PDG/PDG/PDG.h"
#include "PDG/PDG/FunctionPDG.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Module.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"


namespace input_dependency {

static llvm::cl::opt<std::string> libfunction_config(
    "lib-config",
    llvm::cl::desc("Configuration file for library functions"),
    llvm::cl::value_desc("file name"));

// TODO: add other cmd line options too

void configure_run()
{
    InputDepConfig::get().set_lib_config_file(libfunction_config);
}

void InputDependencyAnalysisPass::getAnalysisUsage(llvm::AnalysisUsage& AU) const
{
    AU.setPreservesCFG();
    AU.addRequired<GraphBuilderPass>();
    AU.addRequired<llvm::CallGraphWrapperPass>();
    AU.addPreserved<llvm::CallGraphWrapperPass>();
    AU.setPreservesAll();
}

bool InputDependencyAnalysisPass::runOnModule(llvm::Module& M)
{
    configure_run();
    auto pdg = getAnalysis<GraphBuilderPass>().getPDG();
    llvm::CallGraph* CG = &getAnalysis<llvm::CallGraphWrapperPass>().getCallGraph();
    InputDependencyAnalysis inputDepAnalysis(&M);
    inputDepAnalysis.setPDG(pdg);
    inputDepAnalysis.setCallGraph(CG);
    m_inputDepAnalysisRes.reset(&inputDepAnalysis);
    m_inputDepAnalysisRes->analyze();
    return false;
}

char InputDependencyAnalysisPass::ID = 0;
static llvm::RegisterPass<InputDependencyAnalysisPass> X("input-dependency","Perform input dependency reachability analysis");


} // namespace input_dependency
