#include "pch.h"

#include "IDE/Shell/ShellLayout.h"

namespace VisualForge::IDE::Shell
{
    ShellLayout ShellLayout::CreateDefault()
    {
        ShellLayout layout;

        layout.m_commands = {
            { L"file.open", L"Open Folder", L"Ctrl+O", L"\xE8E5" },
            { L"file.saveAll", L"Save All", L"Ctrl+Shift+S", L"\xE74E" },
            { L"build.buildSolution", L"Build Solution", L"Ctrl+Shift+B", L"\xE768" },
            { L"debug.start", L"Start Debugging", L"F5", L"\xE768" },
            { L"debug.stepOver", L"Step Over", L"F10", L"\xE7FD" },
            { L"git.sync", L"Sync Repository", L"Ctrl+Shift+G", L"\xE895" },
            { L"tools.options", L"Options", L"Ctrl+,", L"\xE713" },
        };

        layout.m_toolWindows = {
            { L"solutionExplorer", L"Solution Explorer", DockSide::Right, true },
            { L"gitChanges", L"Git Changes", DockSide::Right, true },
            { L"properties", L"Properties", DockSide::Right, false },
            { L"errorList", L"Error List", DockSide::Bottom, true },
            { L"output", L"Output", DockSide::Bottom, true },
            { L"terminal", L"Developer PowerShell", DockSide::Bottom, true },
            { L"breakpoints", L"Breakpoints", DockSide::Bottom, false },
            { L"toolbox", L"Toolbox", DockSide::Left, false },
            { L"settings", L"Options", DockSide::Document, true },
        };

        layout.m_documents = {
            { L"Options", L"VisualForge://settings/text-editor", false },
            { L"MainView.xaml", L"UI/Xaml/View/Page/MainView.xaml", true },
            { L"MSBuildAdapter.cpp", L"Integration/MSBuildAdapter.cpp", false },
        };

        layout.m_settingsPages = {
            { L"Environment", L"General", { L"Theme", L"Window layout", L"Keyboard profile", L"Startup" } },
            { L"Text Editor", L"General", { L"Drag and drop text editing", L"Track changes", L"Auto-detect UTF-8", L"Show whitespace", L"Brace pair colorization" } },
            { L"Projects and Solutions", L"Build and Run", { L"MSBuild verbosity", L"Parallel project builds", L"Compile commands generation" } },
            { L"Debugging", L"General", { L"DAP adapter path", L"Stop at entry", L"NatVis loading", L"Symbol servers" } },
            { L"Package Managers", L"Sources", { L"NuGet feeds", L"vcpkg registries", L"Triplet defaults" } },
        };

        return layout;
    }

    std::vector<ShellCommand> const& ShellLayout::Commands() const noexcept
    {
        return m_commands;
    }

    std::vector<ToolWindowDescriptor> const& ShellLayout::ToolWindows() const noexcept
    {
        return m_toolWindows;
    }

    std::vector<DocumentDescriptor> const& ShellLayout::Documents() const noexcept
    {
        return m_documents;
    }

    std::vector<SettingsDescriptor> const& ShellLayout::SettingsPages() const noexcept
    {
        return m_settingsPages;
    }

    std::wstring_view ToDockSideName(DockSide side) noexcept
    {
        switch (side)
        {
        case DockSide::Left:
            return L"Left";
        case DockSide::Right:
            return L"Right";
        case DockSide::Bottom:
            return L"Bottom";
        case DockSide::Document:
            return L"Document";
        default:
            return L"Unknown";
        }
    }
}
