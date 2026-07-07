#include "pch.h"

#include "Integration/MSBuildAdapter.h"

namespace VisualForge::Integration
{
    namespace
    {
        std::filesystem::path FindCompileCommands(std::filesystem::path const& root)
        {
            if (root.empty() || !std::filesystem::exists(root))
            {
                return {};
            }

            auto const direct = root / L"compile_commands.json";
            if (std::filesystem::exists(direct))
            {
                return direct;
            }

            std::error_code error;
            for (auto const& entry : std::filesystem::recursive_directory_iterator{ root, std::filesystem::directory_options::skip_permission_denied, error })
            {
                if (error)
                {
                    break;
                }

                if (entry.path().filename() == L"compile_commands.json")
                {
                    return entry.path();
                }
            }

            return {};
        }
    }

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

    Tool::ToolCommand MSBuildAdapter::CreateEvaluationCommand(ProjectContext const& context, std::filesystem::path outputPath) const
    {
        auto buildPath = SelectBuildPath(context);
        if (outputPath.empty())
        {
            outputPath = context.WorkspaceRoot / L".visualforge" / L"msbuild-evaluation.xml";
        }

        std::vector<std::wstring> arguments{
            buildPath.wstring(),
            L"/nologo",
            L"/pp:" + outputPath.wstring(),
            L"/p:GenerateCompileCommands=true",
            L"/p:SkipCompilerExecution=true"
        };

        auto properties = CreateConfigurationProperties(context);
        arguments.insert(arguments.end(), properties.begin(), properties.end());

        return m_registry.CreateCommand(Tool::ToolKind::MSBuild, std::move(arguments), context.WorkspaceRoot, L"Evaluate MSBuild property graph");
    }

    CommandPlan MSBuildAdapter::CreateLanguageServicePreparationPlan(ProjectContext const& context) const
    {
        return {
            L"Prepare C++ language service",
            L"Restore the MSBuild graph, evaluate imported props/targets, generate compile_commands.json, then start clangd.",
            {
                CreateRestoreCommand(context),
                CreateEvaluationCommand(context),
                CreateGenerateCompileCommandsCommand(context)
            }
        };
    }

    MSBuildEvaluationResult MSBuildAdapter::EvaluateProject(ProjectContext const& context, unsigned long timeoutMs) const
    {
        auto outputPath = context.WorkspaceRoot / L".visualforge" / L"msbuild-evaluation.xml";
        if (!outputPath.parent_path().empty())
        {
            std::filesystem::create_directories(outputPath.parent_path());
        }

        auto command = CreateEvaluationCommand(context, outputPath);
        Tool::ProcessSession process;
        MSBuildEvaluationResult result;
        result.EvaluationXmlPath = outputPath;
        result.Started = process.Start(command);
        if (!result.Started)
        {
            return result;
        }

        result.Completed = process.WaitForExit(timeoutMs);
        auto snapshot = process.Snapshot();
        result.ExitCode = snapshot.ExitCode;
        result.Output = std::move(snapshot.StdOut);
        result.Error = std::move(snapshot.StdErr);
        result.CompileCommandsPath = FindCompileCommands(context.CompileCommandsDirectory.empty()
            ? context.WorkspaceRoot
            : context.CompileCommandsDirectory);
        return result;
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
