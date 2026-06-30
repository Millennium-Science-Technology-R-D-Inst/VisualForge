#include "pch.h"

#include "Integration/ClangdAdapter.h"

namespace VisualForge::Integration
{
    ClangdAdapter::ClangdAdapter(Tool::ToolRegistry registry) :
        m_registry(std::move(registry))
    {
    }

    AdapterSnapshot ClangdAdapter::Describe() const
    {
        return {
            L"ClangdAdapter",
            L"Language service bridge backed by compile_commands.json.",
            AdapterStatus::RequiresProject,
            {
                { L"LSP", L"Starts clangd with background indexing and clang-tidy enabled." },
                { L"Compile database", L"Uses MSBuild-generated compile_commands.json when available." },
                { L"Diagnostics", L"Can run clangd --check for a single translation unit." }
            }
        };
    }

    Tool::ToolCommand ClangdAdapter::CreateStartCommand(ProjectContext const& context) const
    {
        std::vector<std::wstring> arguments{
            L"--background-index",
            L"--clang-tidy",
            L"--header-insertion=iwyu",
            L"--completion-style=detailed"
        };

        auto compileCommandsDirectory = context.CompileCommandsDirectory;
        if (compileCommandsDirectory.empty() && !context.ProjectPath.empty())
        {
            compileCommandsDirectory = context.ProjectPath.parent_path();
        }

        if (!compileCommandsDirectory.empty())
        {
            arguments.push_back(L"--compile-commands-dir=" + compileCommandsDirectory.wstring());
        }

        return m_registry.CreateCommand(Tool::ToolKind::Clangd, std::move(arguments), context.WorkspaceRoot, L"Start clangd");
    }

    Tool::ToolCommand ClangdAdapter::CreateCheckCommand(ProjectContext const& context, std::filesystem::path const& sourceFile) const
    {
        std::vector<std::wstring> arguments{
            L"--check=" + sourceFile.wstring()
        };

        if (!context.CompileCommandsDirectory.empty())
        {
            arguments.push_back(L"--compile-commands-dir=" + context.CompileCommandsDirectory.wstring());
        }

        return m_registry.CreateCommand(Tool::ToolKind::Clangd, std::move(arguments), context.WorkspaceRoot, L"Check translation unit");
    }
}
