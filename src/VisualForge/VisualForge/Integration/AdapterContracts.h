#pragma once

#include "Tool/ToolCommand.h"

#include <filesystem>
#include <string>
#include <vector>

namespace VisualForge::Integration
{
    enum class AdapterStatus
    {
        NotConfigured,
        Ready,
        RequiresProject
    };

    struct AdapterCapability
    {
        std::wstring Name;
        std::wstring Detail;
    };

    struct AdapterSnapshot
    {
        std::wstring Name;
        std::wstring Summary;
        AdapterStatus Status{ AdapterStatus::NotConfigured };
        std::vector<AdapterCapability> Capabilities;
    };

    struct ProjectContext
    {
        std::filesystem::path WorkspaceRoot;
        std::filesystem::path SolutionPath;
        std::filesystem::path ProjectPath;
        std::filesystem::path CompileCommandsDirectory;
        std::wstring Configuration{ L"Debug" };
        std::wstring Platform{ L"x64" };
    };

    struct CommandPlan
    {
        std::wstring Title;
        std::wstring Detail;
        std::vector<Tool::ToolCommand> Commands;
    };
}
