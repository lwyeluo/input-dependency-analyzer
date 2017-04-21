#include "ReflectingBasicBlockAnaliser.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace input_dependency {

namespace {

DependencyAnaliser::ValueDependencies mergeValueDependencies(const std::vector<DependencyAnaliser::ValueDependencies>& dependencies)
{
    DependencyAnaliser::ValueDependencies mergedDependencies;
    for (const auto& deps : dependencies) {
        for (const auto& valDeps : deps) {
            assert(!valDeps.second.isValueDep());
            auto res = mergedDependencies.insert(valDeps);
            if (res.second) {
                continue;
            }
            res.first->second.mergeDependencies(valDeps.second);
        }
    }
    return mergedDependencies;
}

DependencyAnaliser::ValueDependencies mergeSuccessorDependenciesWithInitialDependencies(
                                                    const DependencyAnaliser::ValueDependencies& initialDependencies,
                                                    const DependencyAnaliser::ValueDependencies& mergeInto)
{
    auto finalMerged = mergeInto;
    for (const auto& initialDep : initialDependencies) {
        finalMerged.insert(initialDep);
    }
    return finalMerged;
}

} // unnamed namespace

ReflectingBasicBlockAnaliser::ReflectingBasicBlockAnaliser(
                        llvm::Function* F,
                        llvm::AAResults& AAR,
                        const Arguments& inputs,
                        const FunctionAnalysisGetter& Fgetter,
                        llvm::BasicBlock* BB)
                    : BasicBlockAnalysisResult(F, AAR, inputs, Fgetter, BB)
                    , m_isReflected(false)
{
}

// DUBUG
//void ReflectingBasicBlockAnaliser::dumpResults() const
//{
//    BasicBlockAnalysisResult::dumpResults();
//    llvm::dbgs() << "Value dependencies\n";
//    for (const auto& val : m_valueDependencies) {
//        llvm::dbgs() << "Value " << *val.first << val.second.getDependencyName() << "\n";
//        for (const auto& v : val.second.getValueDependencies()) {
//            llvm::dbgs() << "       " << *v  << "\n";
//        }
//        llvm::dbgs() << "Input dependencies\n";
//        for (const auto& dep : val.second.getArgumentDependencies()) {
//            llvm::dbgs() << "       " << *dep  << "\n";
//        }
//    }
//
//    llvm::dbgs() << "Value dependent instructions\n";
//    for (const auto& item : m_instructionValueDependencies) {
//        llvm::dbgs() << *item.first << "  " << item.second.getDependencyName() << "\n";
//        for (const auto& dep : item.second.getValueDependencies()) {
//            llvm::dbgs() << "       " << *dep  << "\n";
//        }
//    }
//
//    llvm::dbgs() << "Value dependent values\n";
//    for (const auto& item : m_valueDependencies) {
//        if (!item.second.isValueDep()) {
//            continue;
//        }
//        llvm::dbgs() << *item.first << "   " << item.second.getDependencyName() << "\n";
//        for (const auto& dep : item.second.getValueDependencies()) {
//            llvm::dbgs() << "       " << *dep << "\n";
//        }
//    }
//
//    llvm::dbgs() << "Value dependent out arguments\n";
//    for (const auto& item : m_valueDependentOutArguments) {
//        for (const auto& arg : item.second) {
//            llvm::dbgs() << *arg << " depends on " << *item.first << "\n"; 
//        }
//    }
//    
//    llvm::dbgs() << "Value dependent called functions arguments\n";
//    for (const auto& item : m_valueDependentFunctionCallArguments) {
//        for (const auto& fdep : item.second) {
//            llvm::dbgs() << "Function " << fdep.first->getName() << " argument \n";
//            for (const auto& dep : fdep.second) {
//                llvm::dbgs() << *dep << " depends on value " << *item.first << "\n";
//            }
//        }
//    }
//
//    llvm::dbgs() << "\n";
//}

void ReflectingBasicBlockAnaliser::reflect(const DependencyAnaliser::ValueDependencies& dependencies)
{
    resolveValueDependencies(dependencies);
    for (auto& item : m_valueDependencies) {
        if (!item.second.isDefined()) {
            continue;
        }
        reflect(item.first, item.second);
    }
    // TODO: would not need this part remove if all instructions are collected together in one map
    for (auto& instrDep : m_instructionValueDependencies) {
        assert(instrDep.second.isValueDep());
        m_inputDependentInstrs[instrDep.first].mergeDependencies(instrDep.second);
    }
    m_instructionValueDependencies.clear();
    m_valueDependentInstrs.clear();

    assert(m_valueDependentInstrs.empty());
    assert(m_valueDependentOutArguments.empty());
    assert(m_valueDependentFunctionCallArguments.empty());
    assert(m_valueDependentFunctionInvokeArguments.empty());
    m_isReflected = true;
}

void ReflectingBasicBlockAnaliser::setInitialValueDependencies(
                    const DependencyAnalysisResult::InitialValueDpendencies& valueDependencies)
{
    BasicBlockAnalysisResult::setInitialValueDependencies(valueDependencies);
    for (auto& item : m_valueDependencies) {
        if (!item.second.isValueDep()) {
            continue;
        }
        DepInfo finalDeps(item.second.getDependency());
        for (const auto& dep : item.second.getValueDependencies()) {
            const auto& finaldeps = getValueFinalDependencies(dep);
            finalDeps.mergeDependencies(finaldeps);
        }
        item.second.setValueDependencies(finalDeps.getValueDependencies());
        item.second.mergeDependencies(finalDeps);
    }
}

DepInfo ReflectingBasicBlockAnaliser::getInstructionDependencies(llvm::Instruction* instr) const
{
    auto indeppos = m_inputIndependentInstrs.find(instr);
    if (indeppos != m_inputIndependentInstrs.end()) {
        return DepInfo(DepInfo::INPUT_INDEP);
    }
    auto valpos = m_instructionValueDependencies.find(instr);
    if (valpos != m_instructionValueDependencies.end()) {
        return valpos->second;
    }
    auto deppos = m_inputDependentInstrs.find(instr);
    assert(deppos != m_inputDependentInstrs.end());
    assert(deppos->second.isInputDep() || deppos->second.isInputArgumentDep());
    return deppos->second;
}

void ReflectingBasicBlockAnaliser::processInstrForOutputArgs(llvm::Instruction* I)
{
    if (m_outArgDependencies.empty()) {
        return;
    }
    const auto& DL = I->getModule()->getDataLayout();
    auto item = m_outArgDependencies.begin();
    while (item != m_outArgDependencies.end()) {
        llvm::Value* val = llvm::dyn_cast<llvm::Value>(item->first);
        const auto& info = m_AAR.getModRefInfo(I, val, DL.getTypeStoreSize(val->getType()));
        if (info != llvm::ModRefInfo::MRI_Mod) {
            ++item;
            continue;
        }
        auto valueDepPos = m_instructionValueDependencies.find(I);
        if (valueDepPos == m_instructionValueDependencies.end()) {
            assert(valueDepPos->second.isValueDep());
            const auto& dependencies = valueDepPos->second;
            item->second.setDependency(valueDepPos->second.getDependency());
            item->second.mergeDependencies(valueDepPos->second.getValueDependencies());
            for (const auto& val : item->second.getValueDependencies()) {
                m_valueDependentOutArguments[val].insert(item->first);
            }
            continue;
        }
        auto depInstrPos = m_inputDependentInstrs.find(I);
        if (depInstrPos != m_inputDependentInstrs.end()) {
            const auto& dependencies = depInstrPos->second;
            item->second.mergeDependencies(depInstrPos->second);
        } else {
            // making output input independent
            item->second = DepInfo(DepInfo::INPUT_INDEP);
        }
        ++item;
    }
}

DepInfo ReflectingBasicBlockAnaliser::getInstructionDependencies(llvm::Instruction* instr)
{
    auto deppos = m_inputDependentInstrs.find(instr);
    if (deppos != m_inputDependentInstrs.end()) {
        return deppos->second;
    }
    auto indeppos = m_inputIndependentInstrs.find(instr);
    if (indeppos != m_inputIndependentInstrs.end()) {
        return DepInfo(DepInfo::INPUT_INDEP);
    }
    auto valdeppos = m_instructionValueDependencies.find(instr);
    if (valdeppos != m_instructionValueDependencies.end()) {
        return valdeppos->second;
    }
    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(instr)) {
        auto deps = m_valueDependencies[allocaInst];
        DepInfo info;
        info.mergeDependencies(deps);
        return info;
    }
    if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(instr)) {
        return getLoadInstrDependencies(loadInst);
    }

    return determineInstructionDependenciesFromOperands(instr);

}

void ReflectingBasicBlockAnaliser::updateInstructionDependencies(llvm::Instruction* instr,
                                                                 const DepInfo& info)
{
    assert(info.isDefined());
    if (info.isValueDep()) {
        m_instructionValueDependencies[instr] = info;
        updateValueDependentInstructions(info, instr);
    } else if (info.isInputIndep()) {
        assert(info.getArgumentDependencies().empty());
        assert(info.getValueDependencies().empty());
        m_inputIndependentInstrs.insert(instr);
    } else {
        assert(info.isInputDep() || info.isInputArgumentDep());
        m_inputDependentInstrs[instr] = info;
    }
}

// basic block analysis result could do this same way
void ReflectingBasicBlockAnaliser::updateReturnValueDependencies(const DepInfo& info)
{
    m_returnValueDependencies.mergeDependencies(info);
}

DepInfo ReflectingBasicBlockAnaliser::getLoadInstrDependencies(llvm::LoadInst* instr)
{
    auto* loadOp = instr->getPointerOperand();
    llvm::Value* loadedValue = getMemoryValue(loadOp);

    DepInfo info = BasicBlockAnalysisResult::getLoadInstrDependencies(instr);
    info.mergeDependencies(ValueSet{loadedValue});
    return info;
}

void ReflectingBasicBlockAnaliser::updateFunctionCallSiteInfo(llvm::CallInst* callInst)
{
    BasicBlockAnalysisResult::updateFunctionCallSiteInfo(callInst);
    updateValueDependentCallArguments(callInst);
    updateValueDependentCallReferencedGlobals(callInst);
}

void ReflectingBasicBlockAnaliser::updateFunctionInvokeSiteInfo(llvm::InvokeInst* invokeInst)
{
    BasicBlockAnalysisResult::updateFunctionInvokeSiteInfo(invokeInst);
    updateValueDependentInvokeArguments(invokeInst);
    updateValueDependentInvokeReferencedGlobals(invokeInst);
}

void ReflectingBasicBlockAnaliser::updateValueDependentInstructions(const DepInfo& info,
                                                                    llvm::Instruction* instr)
{
    for (const auto& val : info.getValueDependencies()) {
        m_valueDependentInstrs[val].insert(instr);
    }
}

void ReflectingBasicBlockAnaliser::updateValueDependentCallArguments(llvm::CallInst* callInst)
{
    auto F = callInst->getCalledFunction();
    assert(F != nullptr);
    auto pos = m_functionCallInfo.find(F);
    if (pos == m_functionCallInfo.end()) {
        // is this possible?
        return;
    }

    const auto& dependencies = pos->second.getArgumentDependenciesForCall(callInst);
    for (const auto& dep : dependencies) {
        if (!dep.second.isValueDep()) {
            continue;
        }
        for (const auto& val : dep.second.getValueDependencies()) {
            m_valueDependentFunctionCallArguments[val][callInst].insert(dep.first);
        }
    }
}

void ReflectingBasicBlockAnaliser::updateValueDependentInvokeArguments(llvm::InvokeInst* invokeInst)
{
    auto F = invokeInst->getCalledFunction();
    assert(F != nullptr);
    auto pos = m_functionCallInfo.find(F);
    assert(pos != m_functionCallInfo.end());
    const auto& dependencies = pos->second.getArgumentDependenciesForInvoke(invokeInst);
    for (const auto& dep : dependencies) {
        if (!dep.second.isValueDep()) {
            continue;
        }
        for (const auto& val : dep.second.getValueDependencies()) {
            m_valueDependentFunctionInvokeArguments[val][invokeInst].insert(dep.first);
        }
    }
}

void ReflectingBasicBlockAnaliser::updateValueDependentCallReferencedGlobals(llvm::CallInst* callInst)
{
    auto F = callInst->getCalledFunction();
    assert(F != nullptr);
    auto pos = m_functionCallInfo.find(F);
    assert(pos != m_functionCallInfo.end());
    const auto& dependencies = pos->second.getGlobalsDependenciesForCall(callInst);
    for (const auto& dep : dependencies) {
        if (!dep.second.isValueDep()) {
            continue;
        }
        for (const auto& val : dep.second.getValueDependencies()) {
            m_valueDependentCallGlobals[val][callInst].insert(dep.first);
        }
    }
}

void ReflectingBasicBlockAnaliser::updateValueDependentInvokeReferencedGlobals(llvm::InvokeInst* invokeInst)
{
    auto F = invokeInst->getCalledFunction();
    assert(F != nullptr);
    auto pos = m_functionCallInfo.find(F);
    assert(pos != m_functionCallInfo.end());
    const auto& dependencies = pos->second.getGlobalsDependenciesForInvoke(invokeInst);
    for (const auto& dep : dependencies) {
        if (!dep.second.isValueDep()) {
            continue;
        }
        for (const auto& val : dep.second.getValueDependencies()) {
            m_valueDependentInvokeGlobals[val][invokeInst].insert(dep.first);
        }
    }
}

void ReflectingBasicBlockAnaliser::reflect(llvm::Value* value, const DepInfo& deps)
{
    assert(deps.isDefined());
    // remove later
    if (deps.isValueDep()) {
        for (const auto& val : deps.getValueDependencies()) {
            assert(llvm::dyn_cast<llvm::GlobalVariable>(val));
        }

    }
    reflectOnInstructions(value, deps); // need to go trough instructions one more time and add to correspoinding set
    reflectOnOutArguments(value, deps);
    reflectOnCalledFunctionArguments(value, deps);
    reflectOnCalledFunctionReferencedGlobals(value, deps);
    reflectOnInvokedFunctionArguments(value, deps);
    reflectOnInvokedFunctionReferencedGlobals(value, deps);
    reflectOnReturnValue(value, deps);
}

void ReflectingBasicBlockAnaliser::reflectOnInstructions(llvm::Value* value, const DepInfo& depInfo)
{
    auto instrDepPos = m_valueDependentInstrs.find(value);
    if (instrDepPos == m_valueDependentInstrs.end()) {
        return;
    }
    for (const auto& instr : instrDepPos->second) {
        auto instrPos = m_instructionValueDependencies.find(instr);
        assert(instrPos != m_instructionValueDependencies.end());
        reflectOnDepInfo(value, instrPos->second, depInfo);
        if (instrPos->second.isValueDep()) {
            continue;
        }
        if (instrPos->second.isInputDep() || instrPos->second.isInputArgumentDep()) {
            m_inputDependentInstrs[instr].mergeDependencies(instrPos->second);
        } else if (instrPos->second.isInputIndep()) {
            m_inputIndependentInstrs.insert(instr);
        }
        m_instructionValueDependencies.erase(instrPos);
    }
    m_valueDependentInstrs.erase(instrDepPos);
}

void ReflectingBasicBlockAnaliser::reflectOnOutArguments(llvm::Value* value, const DepInfo& depInfo)
{
    auto outArgPos = m_valueDependentOutArguments.find(value);
    if (outArgPos == m_valueDependentOutArguments.end()) {
        return;
    }
    for (const auto& outArg : outArgPos->second) {
        auto argPos = m_outArgDependencies.find(outArg);
        assert(argPos != m_outArgDependencies.end());
        reflectOnDepInfo(value, argPos->second, depInfo);
    }
    m_valueDependentOutArguments.erase(outArgPos);
}

void ReflectingBasicBlockAnaliser::reflectOnCalledFunctionArguments(llvm::Value* value, const DepInfo& depInfo)
{
    auto valPos = m_valueDependentFunctionCallArguments.find(value);
    if (valPos == m_valueDependentFunctionCallArguments.end()) {
        return;
    }

    for (const auto& fargs : valPos->second) {
        auto callInst = fargs.first;
        auto F = callInst->getCalledFunction();
        auto Fpos = m_functionCallInfo.find(F);
        assert(Fpos != m_functionCallInfo.end());
        auto& callDeps = Fpos->second.getArgumentDependenciesForCall(callInst);
        for (auto& arg : fargs.second) {
            auto argPos = callDeps.find(arg);
            assert(argPos != callDeps.end());
            reflectOnDepInfo(value, argPos->second, depInfo);
            // TODO: need to delete if becomes input indep?
        }
    }
    m_valueDependentFunctionCallArguments.erase(valPos);
}

void ReflectingBasicBlockAnaliser::reflectOnCalledFunctionReferencedGlobals(llvm::Value* value, const DepInfo& depInfo)
{
    auto valPos = m_valueDependentCallGlobals.find(value);
    if (valPos == m_valueDependentCallGlobals.end()) {
        return;
    }

    for (const auto& fargs : valPos->second) {
        auto callInst = fargs.first;
        auto F = callInst->getCalledFunction();
        auto Fpos = m_functionCallInfo.find(F);
        assert(Fpos != m_functionCallInfo.end());
        auto& callDeps = Fpos->second.getGlobalsDependenciesForCall(callInst);
        for (auto& arg : fargs.second) {
            auto argPos = callDeps.find(arg);
            assert(argPos != callDeps.end());
            reflectOnDepInfo(value, argPos->second, depInfo);
        }
    }
    m_valueDependentCallGlobals.erase(valPos);
}

void ReflectingBasicBlockAnaliser::reflectOnInvokedFunctionArguments(llvm::Value* value, const DepInfo& depInfo)
{
    auto valPos = m_valueDependentFunctionInvokeArguments.find(value);
    if (valPos == m_valueDependentFunctionInvokeArguments.end()) {
        return;
    }

    for (const auto& fargs : valPos->second) {
        auto invokeInst = fargs.first;
        auto F = invokeInst->getCalledFunction();
        auto Fpos = m_functionCallInfo.find(F);
        assert(Fpos != m_functionCallInfo.end());
        auto& invokeDeps = Fpos->second.getArgumentDependenciesForInvoke(invokeInst);
        for (auto& arg : fargs.second) {
            auto argPos = invokeDeps.find(arg);
            assert(argPos != invokeDeps.end());
            reflectOnDepInfo(value, argPos->second, depInfo);
            // TODO: need to delete if becomes input indep?
        }
    }
    m_valueDependentFunctionInvokeArguments.erase(valPos);
}

void ReflectingBasicBlockAnaliser::reflectOnInvokedFunctionReferencedGlobals(llvm::Value* value, const DepInfo& depInfo)
{
    auto valPos = m_valueDependentInvokeGlobals.find(value);
    if (valPos == m_valueDependentInvokeGlobals.end()) {
        return;
    }

    for (const auto& fargs : valPos->second) {
        auto invokeInst = fargs.first;
        auto F = invokeInst->getCalledFunction();
        auto Fpos = m_functionCallInfo.find(F);
        assert(Fpos != m_functionCallInfo.end());
        auto& invokeDeps = Fpos->second.getGlobalsDependenciesForInvoke(invokeInst);
        for (auto& arg : fargs.second) {
            auto argPos = invokeDeps.find(arg);
            assert(argPos != invokeDeps.end());
            reflectOnDepInfo(value, argPos->second, depInfo);
        }
    }
    m_valueDependentInvokeGlobals.erase(valPos);
}

void ReflectingBasicBlockAnaliser::reflectOnReturnValue(llvm::Value* value, const DepInfo& depInfo)
{
    if (!m_returnValueDependencies.isValueDep()) {
        return;
    }
    auto pos = m_returnValueDependencies.getValueDependencies().find(value);
    if (pos == m_returnValueDependencies.getValueDependencies().end()) {
        return;
    }
    reflectOnDepInfo(value, m_returnValueDependencies, depInfo);
}

void ReflectingBasicBlockAnaliser::reflectOnSingleValue(llvm::Value* value, DepInfo& valueDep)
{
    if (!valueDep.isValueDep()) {
        return;
    }
    auto& valDeps = valueDep.getValueDependencies(); 
    std::vector<llvm::Value*> globals;
    ValueSet processedValues;
    while (!valDeps.empty()) {
        auto val = *valDeps.begin();
        processedValues.insert(val);
        if (val == value) {
            auto num = valDeps.erase(val);
            assert(num > 0);
            if (valDeps.empty()) {
                valueDep.mergeDependency(DepInfo::INPUT_INDEP);
                break;
            }
            continue;
        }
        auto pos = m_valueDependencies.find(val);
        if (pos == m_valueDependencies.end()) {
            assert(llvm::dyn_cast<llvm::GlobalVariable>(val));
            globals.push_back(val);
            valDeps.erase(val);
            continue;
        }
        valueDep.mergeDependencies(pos->second.getArgumentDependencies());
        if (valueDep.getDependency() == DepInfo::VALUE_DEP) {
            valueDep.setDependency(pos->second.getDependency());
        } else {
            valueDep.mergeDependency(pos->second.getDependency());
        }
        for (const auto& v : pos->second.getValueDependencies()) {
            if (processedValues.find(v) == processedValues.end()) {
                valDeps.insert(v);
            }
        }
        auto num = valDeps.erase(val);
        assert(num > 0);
    }
    if (!globals.empty()) {
        valueDep.mergeDependencies(DepInfo(DepInfo::VALUE_DEP, ValueSet(globals.begin(), globals.end())));
    }
}

void ReflectingBasicBlockAnaliser::reflectOnDepInfo(llvm::Value* value,
                                                    DepInfo& depInfoTo,
                                                    const DepInfo& depInfoFrom,
                                                    bool eraseAfterReflection)
{
    // note: this won't change pos dependency, if it is of maximum value input_dep
    assert(depInfoTo.isValueDep());
    if (depInfoTo.getDependency() == DepInfo::VALUE_DEP) {
        depInfoTo.setDependency(depInfoFrom.getDependency());
    }
    depInfoTo.mergeDependencies(depInfoFrom);
    if (!eraseAfterReflection) {
        return;
    }
    auto& valueDeps = depInfoTo.getValueDependencies();
    auto valPos = valueDeps.find(value);
    assert(valPos != valueDeps.end());
    valueDeps.erase(valPos);
}

void ReflectingBasicBlockAnaliser::resolveValueDependencies(const DependencyAnaliser::ValueDependencies& successorDependencies)
{
    for (const auto& dep : successorDependencies) {
        auto res = m_valueDependencies.insert(dep);
        if (!res.second) {
            res.first->second.mergeDependencies(dep.second);
        }
    }
    for (auto& item : m_valueDependencies) {
        reflectOnSingleValue(item.first, item.second);
    }
}

DepInfo ReflectingBasicBlockAnaliser::getValueFinalDependencies(llvm::Value* value)
{
    auto pos = m_valueDependencies.find(value);
    if (pos == m_valueDependencies.end()) {
        assert(llvm::dyn_cast<llvm::GlobalVariable>(value));
        return DepInfo(DepInfo::VALUE_DEP, ValueSet{value});
    }
    assert(pos != m_valueDependencies.end());
    if (pos->second.getValueDependencies().empty()) {
        return DepInfo(pos->second.getDependency(), ValueSet{value});
    }
    DepInfo depInfo(pos->second.getDependency());
    for (auto val : pos->second.getValueDependencies()) {
        if (val == value) {
            // ???
            depInfo.mergeDependencies(ValueSet{value});
            continue;
        }
        const auto& deps = getValueFinalDependencies(val);
        depInfo.mergeDependencies(deps);
    }
    return depInfo;
}


} // namespace input_dependency

