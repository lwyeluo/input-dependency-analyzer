#include "LibFunctionInfo.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"


namespace {

using IndexToArgumentMap = input_dependency::LibFunctionInfo::IndexToArgumentMap;
using LibArgDepInfo = input_dependency::LibFunctionInfo::LibArgDepInfo;
using ArgumentSet = input_dependency::ArgumentSet;

IndexToArgumentMap getFunctionIndexToArgMap(llvm::Function* F)
{
    std::unordered_map<int, llvm::Argument*> indexToArg;
    for (auto& arg : F->getArgumentList()) {
        indexToArg.emplace(arg.getArgNo(), &arg);
    }
    return indexToArg;
}

ArgumentSet getResolvedArguments(const IndexToArgumentMap& indexToArg,
                                 const LibArgDepInfo& libDepInfo)
{
    ArgumentSet arguments;
    for (const auto& dep : libDepInfo.argumentDependencies) {
        auto pos = indexToArg.find(dep);
        assert(pos != indexToArg.end());
        arguments.emplace(pos->second);
    }
    return arguments;
}

}

namespace input_dependency {

LibFunctionInfo::LibFunctionInfo(const std::string& name,
                                 const LibArgumentDependenciesMap& argumentDeps,
                                 const LibArgDepInfo& retDep)
    : m_name(name)
    , m_isResolved(false)
    , m_argumentDependencies(argumentDeps)
    , m_returnDependency(retDep)
{
}

LibFunctionInfo::LibFunctionInfo(std::string&& name,
                                 LibArgumentDependenciesMap&& argumentDeps,
                                 LibArgDepInfo&& retDep)
    : m_name(std::move(name))
    , m_isResolved(false)
    , m_argumentDependencies(std::move(argumentDeps))
    , m_returnDependency(std::move(retDep))
{
}

const std::string& LibFunctionInfo::getName() const
{
    return m_name;
}

const bool LibFunctionInfo::isResolved() const
{
    return m_isResolved;
}

const LibFunctionInfo::LibArgumentDependenciesMap& LibFunctionInfo::getArgumentDependencies() const
{
    return m_argumentDependencies;
}

const LibFunctionInfo::LibArgDepInfo& LibFunctionInfo::getArgumentDependencies(int index) const
{
    auto pos = m_argumentDependencies.find(index);
    assert(pos != m_argumentDependencies.end());
    return pos->second;
}

const LibFunctionInfo::LibArgDepInfo& LibFunctionInfo::getReturnDependency() const
{
    return m_returnDependency;
}

const LibFunctionInfo::ArgumentDependenciesMap& LibFunctionInfo::getResolvedArgumentDependencies() const
{
    assert(m_isResolved);
    return m_resolvedArgumentDependencies;
}

const DepInfo& LibFunctionInfo::getResolvedArgumentDependencies(llvm::Argument* arg) const
{
    assert(m_isResolved);
    auto pos = m_resolvedArgumentDependencies.find(arg);
    return pos->second;
}
const DepInfo& LibFunctionInfo::getResolvedReturnDependency() const
{
    assert(m_isResolved);
    return m_resolvedReturnDependency;
}

void LibFunctionInfo::resolve(llvm::Function* F)
{
    const auto& indexToArg = getFunctionIndexToArgMap(F);
    resolveArgumentDependencies(indexToArg);
    resolveReturnDependency(indexToArg);
    m_isResolved = true;
}

void LibFunctionInfo::resolveArgumentDependencies(const IndexToArgumentMap& indexToArg)
{
    for (const auto& argDeps : m_argumentDependencies) {
        auto argPos = indexToArg.find(argDeps.first);
        assert(argPos != indexToArg.end());
        auto& resolvedDeps = m_resolvedArgumentDependencies[argPos->second];
        resolvedDeps.setDependency(argDeps.second.dependency);
        ArgumentSet arguments = getResolvedArguments(indexToArg, argDeps.second);
        resolvedDeps.setArgumentDependencies(std::move(arguments));
    }
    m_argumentDependencies.clear();
}

void LibFunctionInfo::resolveReturnDependency(const IndexToArgumentMap& indexToArg)
{
    m_resolvedReturnDependency.setDependency(m_returnDependency.dependency);
    ArgumentSet arguments = getResolvedArguments(indexToArg, m_returnDependency);
    m_resolvedReturnDependency.setArgumentDependencies(std::move(arguments));
    m_returnDependency.argumentDependencies.clear();
    m_returnDependency.dependency = DepInfo::UNKNOWN;
}

} // namespace input_dependency

