#include "pch.h"
#include "MainViewModel.h"
#if __has_include("ViewModels/MainViewModel.g.cpp")
#include "ViewModels/MainViewModel.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::VisualForge::ViewModels::implementation
{
    MainViewModel::MainViewModel()
    {
        m_workspace = ::VisualForge::IDE::Shell::ShellWorkspace::CreateDefault();
        m_runtime.Reset();
        RefreshDesignTimeStatus();
    }

    hstring MainViewModel::LanguageServiceStatus() const
    {
        return hstring{ m_runtime.LanguageServiceStatusText() };
    }

    hstring MainViewModel::BuildStatus() const
    {
        return hstring{ m_runtime.BuildStatusText() };
    }

    hstring MainViewModel::DebugStatus() const
    {
        return hstring{ m_runtime.DebugStatusText() };
    }

    hstring MainViewModel::ProblemsStatus() const
    {
        return hstring{ m_runtime.ProblemsStatusText() };
    }

    hstring MainViewModel::OutputPreview() const
    {
        return hstring{ m_runtime.OutputPreviewText() };
    }

    hstring MainViewModel::ToolWindowTitle(hstring const& contentId) const
    {
        auto const content = m_workspace.Docking().FindContent(std::wstring{ contentId });
        return hstring{ content ? content->Title : std::wstring{ contentId } };
    }

    hstring MainViewModel::ToolWindowState(hstring const& contentId) const
    {
        if (auto state = m_workspace.FindToolWindowState(std::wstring{ contentId }))
        {
            auto text = std::wstring{ ::VisualForge::IDE::Shell::ToDockStateName(state->State) };
            text += L" / ";
            text += ::VisualForge::IDE::Shell::ToDockSideName(state->CurrentDock);
            return hstring{ text };
        }

        return hstring{ L"Unknown" };
    }

    void MainViewModel::RefreshDesignTimeStatus()
    {
        m_runtime.AppendOutput(L"Shell", L"Visual Studio style shell layout loaded.");
        m_runtime.AppendOutput(L"Docking", L"Tool windows support dock, auto-hide, float, close, and document tab targets.");
        m_runtime.AppendOutput(L"Editor", L"Piece table, viewport, DirectWrite layout, input, search, LSP, and DAP layers are registered.");
    }

    void MainViewModel::Shutdown()
    {
    }

    ::VisualForge::IDE::Shell::ShellWorkspace& MainViewModel::Workspace() noexcept
    {
        return m_workspace;
    }

    ::VisualForge::IDE::Runtime::IdeRuntimeState& MainViewModel::Runtime() noexcept
    {
        return m_runtime;
    }
}
