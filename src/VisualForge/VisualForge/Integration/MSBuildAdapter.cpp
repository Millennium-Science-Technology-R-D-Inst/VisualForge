#include "pch.h"

#include "Integration/MSBuildAdapter.h"

namespace VisualForge::Integration
{
    MSBuildAdapter::MSBuildAdapter(Tool::ToolRegistry registry) :
        m_registry(std::move(registry))
    {
    }

    AdapterSnapshot MSBuildAdapter::Describe() const
    {
        return {
            L"MSBuildAdapter",
            L"Project-system bridge for solution restore, build, and compile command generation.",
            AdapterStatus::RequiresProject,
            {
                { L"Evaluation", L"Delegates property graph expansion to MSBuild." },
                { L"Build", L"Builds sln/vcxproj with selected Configuration and Platform." },
                { L"compile_commands", L"Enables GenerateCompileCommands for clangd." }
            }
        };
    }

    Tool::ToolCommand MSBuildAdapter::CreateRestoreCommand(ProjectContext const& context) const
    {
        auto buildPath = SelectBuildPath(context);
        std::vector<std::wstring> arguments{ buildPath.wstring(), L"/restore", L"/nologo" };

        auto properties = CreateConfigurationProperties(context);
        arguments.insert(arguments.end(), properties.begin(), properties.end());

        return m_registry.CreateCommand(Tool::ToolKind::MSBuild, std::move(arguments), context.WorkspaceRoot, L"Restore solution");
    }

    Tool::ToolCommand MSBuildAdapter::CreateBuildCommand(ProjectContext const& context) const
    {
        auto buildPath = SelectBuildPath(context);
        std::vector<std::wstring> arguments{ buildPath.wstring(), L"/t:Build", L"/m", L"/nologo" };

        auto properties = CreateConfigurationProperties(context);
        arguments.insert(arguments.end(), properties.begin(), properties.end());

        return m_registry.CreateCommand(Tool::ToolKind::MSBuild, std::move(arguments), context.WorkspaceRoot, L"Build project");
    }

    Tool::ToolCommand MSBuildAdapter::CreateGenerateCompileCommandsCommand(ProjectContext const& context) const
    {
        auto buildPath = context.ProjectPath.empty() ? SelectBuildPath(context) : context.ProjectPath;
        std::vector<std::wstring> arguments{
            buildPath.wstring(),
            L"/t:Build",
            L"/p:GenerateCompileCommands=true",
            L"/p:SkipCompilerExecution=true",
            L"/m",
            L"/nologo"
        };

        auto properties = CreateConfigurationProperties(context);
        arguments.insert(arguments.end(), properties.begin(), properties.end());

        return m_registry.CreateCommand(Tool::ToolKind::MSBuild, std::move(arguments), context.WorkspaceRoot, L"Generate compile_commands.json");
    }

    CommandPlan MSBuildAdapter::CreateLanguageServicePreparationPlan(ProjectContext const& context) const
    {
        return {
            L"Prepare C++ language service",
            L"Restore the MSBuild graph, generate compile_commands.json, then start clangd.",
            {
                CreateRestoreCommand(context),
                CreateGenerateCompileCommandsCommand(context)
            }
        };
    }

    std::filesystem::path MSBuildAdapter::SelectBuildPath(ProjectContext const& context)
    {
        if (!context.SolutionPath.empty())
        {
            return context.SolutionPath;
        }

        return context.ProjectPath;
    }

    std::vector<std::wstring> MSBuildAdapter::CreateConfigurationProperties(ProjectContext const& context)
    {
        std::vector<std::wstring> properties;

        if (!context.Configuration.empty())
        {
            properties.push_back(L"/p:Configuration=" + context.Configuration);
        }

        if (!context.Platform.empty())
        {
            properties.push_back(L"/p:Platform=" + context.Platform);
        }

        return properties;
    }
}
