#include "pch.h"

#include "Integration/NuGetAdapter.h"

namespace VisualForge::Integration
{
    NuGetAdapter::NuGetAdapter(Tool::ToolRegistry registry) :
        m_registry(std::move(registry))
    {
    }

    AdapterSnapshot NuGetAdapter::Describe() const
    {
        return {
            L"NuGetAdapter",
            L"Package restore and source inspection for native PackageReference projects.",
            AdapterStatus::Ready,
            {
                { L"Restore", L"Builds nuget restore commands for solution or project files." },
                { L"Sources", L"Reads configured NuGet package sources." }
            }
        };
    }

    Tool::ToolCommand NuGetAdapter::CreateRestoreCommand(std::filesystem::path const& solutionOrProjectPath) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::NuGet,
            { L"restore", solutionOrProjectPath.wstring() },
            solutionOrProjectPath.parent_path(),
            L"Restore NuGet packages");
    }

    Tool::ToolCommand NuGetAdapter::CreateSourcesCommand(std::filesystem::path const& workingDirectory) const
    {
        return m_registry.CreateCommand(Tool::ToolKind::NuGet, { L"sources", L"list" }, workingDirectory, L"List NuGet sources");
    }
}
