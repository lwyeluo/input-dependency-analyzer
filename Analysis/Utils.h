#pragma once

#include "definitions.h"
#include "DependencyAnaliser.h"

namespace input_dependency {

class Utils {

public:
    static bool haveIntersection(const DependencyAnaliser::ArgumentDependenciesMap& inputNums,
                                 const ArgumentSet& selfNums);

    static ValueSet dissolveInstruction(llvm::Instruction* instr);

    static bool isLibraryFunction(llvm::Function* F, llvm::Module* M);
}; // class Utils

} // namespace input_dependency

