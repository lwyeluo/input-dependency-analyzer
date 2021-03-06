#pragma once

#include "FunctionInputDependencyResultInterface.h"

namespace llvm {

class Function;
class BasicBlock;

} // namespace llvm

namespace input_dependency {

class ClonedFunctionAnalysisResult final : public FunctionInputDependencyResultInterface
{
public:
    ClonedFunctionAnalysisResult(llvm::Function* F);

    void setInputDepInstrs(InstrSet&& inputDeps);
    void setInputIndepInstrs(InstrSet&& inputIndeps);
    void setInputDependentBasicBlocks(std::unordered_set<llvm::BasicBlock*>&& inputDeps);
    void setCalledFunctions(const FunctionSet& calledFunctions);
    void setFunctionCallDepInfo(std::unordered_map<llvm::Function*, FunctionCallDepInfo>&& callDepInfo);

public:
     llvm::Function* getFunction() override;
     const llvm::Function* getFunction() const override;
     bool isInputDepFunction() const override;
     void setIsInputDepFunction(bool isInputDep) override;
     bool isExtractedFunction() const override;
     void setIsExtractedFunction(bool isExtracted) override;
     bool isInputDependent(llvm::Instruction* instr) const override;
     bool isInputDependent(const llvm::Instruction* instr) const override;
     bool isInputIndependent(llvm::Instruction* instr) const override;
     bool isInputIndependent(const llvm::Instruction* instr) const override;
     bool isInputDependentBlock(llvm::BasicBlock* block) const override;

     FunctionSet getCallSitesData() const override;
     FunctionCallDepInfo getFunctionCallDepInfo(llvm::Function* F) const override;
     bool changeFunctionCall(const llvm::Instruction* callInstr, llvm::Function* oldF, llvm::Function* newF) override;

    // forstatistics 
     long unsigned get_input_dep_blocks_count() const override;
     long unsigned get_input_indep_blocks_count() const override;
     long unsigned get_unreachable_blocks_count() const override;
     long unsigned get_unreachable_instructions_count() const override;
     long unsigned get_input_dep_count() const override;
     long unsigned get_input_indep_count() const override;
     long unsigned get_input_unknowns_count() const override;

     ClonedFunctionAnalysisResult* toClonedFunctionAnalysisResult() override
     {
         return this;
     }

private:
    llvm::Function* m_F;
    bool m_is_inputDep;
    bool m_is_extracted;
    unsigned int m_instructionsCount;
    InstrSet m_inputIndependentInstrs;
    InstrSet m_inputDependentInstrs;
    FunctionSet m_calledFunctions;
    std::unordered_set<llvm::BasicBlock*> m_inputDependentBasicBlocks;
    std::unordered_map<llvm::Function*, FunctionCallDepInfo> m_functionCallDepInfo;
}; //class ClonedFunctionAnalysisResult

} // namespace input_dependency

