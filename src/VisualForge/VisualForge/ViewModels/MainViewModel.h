#pragma once

#include "IDE/Runtime/IdeRuntimeState.h"
#include "IDE/Shell/ShellWorkspace.h"
#include "ViewModels/MainViewModel.g.h"

namespace winrt::VisualForge::ViewModels::implementation
{
    struct MainViewModel : MainViewModelT<MainViewModel>
    {
        MainViewModel();

        winrt::hstring LanguageServiceStatus() const;
        winrt::hstring BuildStatus() const;
        winrt::hstring DebugStatus() const;
        winrt::hstring ProblemsStatus() const;
        winrt::hstring OutputPreview() const;
        winrt::hstring ToolWindowTitle(winrt::hstring const& contentId) const;
        winrt::hstring ToolWindowState(winrt::hstring const& contentId) const;
        void RefreshDesignTimeStatus();
        void Shutdown();

        ::VisualForge::IDE::Shell::ShellWorkspace& Workspace() noexcept;
        ::VisualForge::IDE::Runtime::IdeRuntimeState& Runtime() noexcept;

    private:
        ::VisualForge::IDE::Shell::ShellWorkspace m_workspace;
        ::VisualForge::IDE::Runtime::IdeRuntimeState m_runtime;
    };
}

namespace winrt::VisualForge::ViewModels::factory_implementation
{
    struct MainViewModel : MainViewModelT<MainViewModel, implementation::MainViewModel>
    {
    };
}
