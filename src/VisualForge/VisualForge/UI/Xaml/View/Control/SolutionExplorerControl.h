#pragma once

#include "UI/Xaml/View/Control/SolutionExplorerControl.g.h"

#include <filesystem>
#include <unordered_map>

namespace winrt::VisualForge::UI::Xaml::View::Control::implementation
{
    struct SolutionExplorerControl : SolutionExplorerControlT<SolutionExplorerControl>
    {
        SolutionExplorerControl();

        void LoadWorkspace(winrt::hstring const& rootPath, winrt::hstring const& solutionPath);
        winrt::event_token FileOpenRequested(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring> const& handler);
        void FileOpenRequested(winrt::event_token const& token) noexcept;

        void WorkspaceTree_SelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::TreeView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::TreeViewSelectionChangedEventArgs const& args);

    private:
        void AddFolderNodes(
            winrt::Microsoft::UI::Xaml::Controls::TreeViewNode const& parent,
            std::filesystem::path const& folder,
            int depth);
        void AddProjectNode(
            winrt::Microsoft::UI::Xaml::Controls::TreeViewNode const& solutionNode,
            std::filesystem::path const& projectPath);

        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring>> m_fileOpenRequested;
        std::unordered_map<void*, std::filesystem::path> m_fileNodes;
    };
}

namespace winrt::VisualForge::UI::Xaml::View::Control::factory_implementation
{
    struct SolutionExplorerControl : SolutionExplorerControlT<SolutionExplorerControl, implementation::SolutionExplorerControl>
    {
    };
}
