#pragma once

#include "IDE/Shell/FloatingWindowHost.h"
#include "EditorCore/Document/DocumentManager.h"
#include "EditorCore/LSP/LspClient.h"
#include "EditorCore/DAP/DapClient.h"
#include "Tool/ProcessSession.h"
#include "EditorCore/Search/SearchEngine.h"
#include "UI/Xaml/View/Control/SolutionExplorerControl.h"
#include "UI/Xaml/View/Control/EditorDocumentControl.h"
#include "ViewModels/MainViewModel.h"
#include "UI/Xaml/View/Page/MainView.g.h"
#include <winrt/Windows.Storage.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace winrt::VisualForge::UI::Xaml::View::Page::implementation
{
    struct MainView : MainViewT<MainView>
    {
        MainView();
        ~MainView();
        void Page_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void InvertAppThemeButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void OpenFile_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void NewFile_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void OpenWorkspace_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void SaveFile_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void SaveAs_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void SaveAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void BuildProject_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void RunProject_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void ContinueDebug_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void StepOverDebug_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void StepInDebug_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void StepOutDebug_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void StopDebug_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void ClearOutput_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void ExecuteTerminal_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void GoToDefinitionKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void HoverKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void ToggleBreakpointKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void EditorTabs_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void EditorTabs_TabCloseRequested(winrt::Microsoft::UI::Xaml::Controls::TabView const& sender, winrt::Microsoft::UI::Xaml::Controls::TabViewTabCloseRequestedEventArgs const& args);
		void SaveKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void NewFileKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void SaveAsKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void Undo_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void Redo_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void GoToLine_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void UndoKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void RedoKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void FindTextBox_TextChanged(winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
		void FindOptions_Changed(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void FindNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void FindPrevious_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void Replace_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void ReplaceAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void FindInFiles_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void SearchResultsList_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void DiagnosticsList_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void CompletionListView_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void FindKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void ReplaceKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void GoToLineKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void QuickOpenKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void QuickOpenBox_TextChanged(winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
		void QuickOpenBox_QuerySubmitted(winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender, winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);
		void RefreshSolutionExplorer_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void WorkspaceModeComboBox_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void EditingLayout_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void DebuggingLayout_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void StartDebuggingKeyboardAccelerator_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
		void RightToolTabs_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void BottomToolTabs_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void RefreshGitChanges_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void GitChangesList_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		void CallStackList_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
		Microsoft::UI::Xaml::Visibility IsDebug();
		void RootPage_PointerPressed(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

        winrt::Windows::Foundation::IAsyncAction LoadBackground();

        VisualForge::ViewModels::MainViewModel ViewModel();

        winrt::event_token CanGoBackChanged(winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, bool> const& handler);
        void CanGoBackChanged(winrt::event_token const& token) noexcept;

        void Navigate(winrt::hstring const& tag);
        void GoBack();
        void GoForward();
        bool CanGoBack() const noexcept;
        bool CanGoForward() const noexcept;
		// Event handlers (XAML wired)
		void AppTitleBar_BackRequested(winrt::Microsoft::UI::Xaml::Controls::TitleBar const&,
									   winrt::Windows::Foundation::IInspectable const&);
        void DockTitleBar_PointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void DockTitleBar_PointerMoved(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void DockTitleBar_PointerReleased(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    private:
        void UpdateNavigationState(bool canGoBack, bool canGoForward);
        void RefreshStatusBar();
        void ShowDockFloatingPreview(bool visible);
        void OpenFloatingToolWindow(winrt::Windows::Foundation::IInspectable const& sender);
		winrt::Windows::Foundation::IAsyncAction OpenFileAsync();
		winrt::Windows::Foundation::IAsyncAction NewFileAsync();
		winrt::Windows::Foundation::IAsyncAction OpenWorkspaceAsync();
		winrt::Windows::Foundation::IAsyncAction OpenPathAsync(std::filesystem::path const& path);
		winrt::Windows::Foundation::IAsyncAction OpenStorageFileAsync(winrt::Windows::Storage::StorageFile const& file);
		winrt::Windows::Foundation::IAsyncAction SaveFileAsync();
		winrt::Windows::Foundation::IAsyncAction SaveAllDocumentsAsync();
		winrt::Windows::Foundation::IAsyncAction SaveDocumentAsync(winrt::Microsoft::UI::Xaml::Controls::TabViewItem const& tab);
		winrt::Windows::Foundation::IAsyncAction SaveDocumentAsAsync(winrt::Microsoft::UI::Xaml::Controls::TabViewItem const& tab);
		winrt::Windows::Foundation::IAsyncAction CloseDocumentAsync(winrt::Microsoft::UI::Xaml::Controls::TabViewItem const& tab);
		winrt::Windows::Foundation::IAsyncAction BuildProjectAsync();
		winrt::Windows::Foundation::IAsyncAction SearchWorkspaceAsync();
		winrt::Windows::Foundation::IAsyncAction OpenSearchResultAsync(std::filesystem::path const& path, std::size_t column, std::size_t length);
		winrt::Windows::Foundation::IAsyncAction OpenDiagnosticAsync(std::filesystem::path const& path, std::size_t line, std::size_t column);
		winrt::Windows::Foundation::IAsyncAction GoToLineAsync();
		winrt::Windows::Foundation::IAsyncAction OpenQuickOpenResultAsync(std::filesystem::path const& path);
		winrt::Windows::Foundation::IAsyncAction RefreshGitChangesAsync();
		winrt::Windows::Foundation::IAsyncAction LoadGitDiffAsync(std::filesystem::path const& path);
		void CloseDocument(winrt::Microsoft::UI::Xaml::Controls::TabViewItem const& tab);
		void UpdateDocumentHeader(void* key);
		void MarkDocumentDirty(void* key);
		void SynchronizeDocument(void* key);
		void ReloadEditorFromDocument(void* key);
		void UndoActiveDocument();
		void RedoActiveDocument();
		void RefreshSearchMatches(bool resetSelection);
		void NavigateSearchMatch(int direction);
		void ReplaceCurrentMatch();
		void ReplaceAllMatches();
		void FocusFindControl(bool replacement);
		void PopulateWorkspaceTree();
		void UpdateEditorPosition();
		[[nodiscard]] std::filesystem::path FindQuickOpenPath(std::wstring_view displayName) const;
		[[nodiscard]] std::filesystem::path FindWorkspaceFile(std::wstring_view extension) const;
		void SetOutput(std::wstring const& source, std::wstring const& text);
		void ParseBuildDiagnostics(std::wstring const& output);
		void ParseGitStatus(std::string const& output);
		void LoadWorkspaceLayouts();
		void ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode mode, bool persist);
		void StartLanguageService();
		void PollLanguageService();
		void PollDebugAdapter();
		void StartTerminalSession();
		void PollTerminal();
		void RequestCompletionForDocument(void* key);
		void ToggleBreakpointForDocument(void* key, int32_t line);
		void RequestHoverForActiveDocument();
		void RequestDefinitionForActiveDocument();

		struct DocumentTabState
		{
			winrt::Windows::Storage::StorageFile File{ nullptr };
			winrt::Microsoft::UI::Xaml::Controls::TabViewItem Tab{ nullptr };
			winrt::VisualForge::UI::Xaml::View::Control::EditorDocumentControl Editor{ nullptr };
			std::filesystem::path Path;
			std::filesystem::path DocumentKey;
			std::wstring LastText;
			bool IsDirty{ false };
		};

		struct WorkspaceSearchResult
		{
			std::filesystem::path Path;
			std::size_t Offset{};
			std::size_t Line{};
			std::size_t Column{};
			std::wstring Preview;
		};

		struct DiagnosticEntry
		{
			std::filesystem::path Path;
			std::size_t Line{};
			std::size_t Column{};
			std::wstring Severity;
			std::wstring Code;
			std::wstring Message;
		};

		struct GitChangeEntry
		{
			std::filesystem::path Path;
			std::string Status;
			std::wstring DisplayName;
		};

		struct CompletionEntry
		{
			std::wstring Label;
			std::wstring Detail;
			std::wstring InsertText;
		};

        VisualForge::ViewModels::MainViewModel m_viewModel{ nullptr };
        bool m_canGoBack{ false };
        bool m_canGoForward{ false };
        bool m_isDockDragPending{ false };
        bool m_isDockDragFloating{ false };
        ::VisualForge::IDE::Shell::FloatingWindowHost m_floatingWindowHost;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, bool>> m_canGoBackChanged;
        winrt::event_token m_canGoBackChangedToken{};
		winrt::Windows::Storage::StorageFile m_activeFile{ nullptr };
		std::filesystem::path m_workspaceRoot;
		std::filesystem::path m_solutionPath;
		std::unordered_map<void*, DocumentTabState> m_documentTabs;
		::VisualForge::EditorCore::Document::DocumentManager m_documentManager;
		bool m_isLoadingDocument{ false };
		bool m_firstDocumentTabInUse{ false };
		std::vector<::VisualForge::EditorCore::Search::SearchMatch> m_searchMatches;
		std::size_t m_searchMatchIndex{};
		std::unordered_map<void*, WorkspaceSearchResult> m_workspaceSearchResults;
		std::unordered_map<void*, DiagnosticEntry> m_diagnosticEntries;
		std::vector<std::filesystem::path> m_quickOpenResults;
		std::vector<std::filesystem::path> m_workspaceFiles;
		std::unordered_map<void*, GitChangeEntry> m_gitChanges;
		std::unordered_map<void*, CompletionEntry> m_completionEntries;
		bool m_isApplyingWorkspaceMode{ false };
		winrt::event_token m_solutionFileOpenToken{};
		::VisualForge::EditorCore::LSP::LspClient m_languageService;
		::VisualForge::EditorCore::DAP::DapClient m_debugAdapter;
		::VisualForge::Tool::ProcessSession m_terminalSession;
		std::size_t m_terminalOutputBytes{ 0 };
		int m_pendingHoverRequestId{ -1 };
		int m_pendingDefinitionRequestId{ -1 };
		std::unordered_map<std::wstring, std::vector<int>> m_breakpoints;
		std::unordered_map<void*, ::VisualForge::EditorCore::DAP::DapStackFrame> m_stackFrames;
		bool m_isPageInitialized{ false };
		winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_languageServiceTimer{ nullptr };
    };
}

namespace winrt::VisualForge::UI::Xaml::View::Page::factory_implementation
{
    struct MainView : MainViewT<MainView, implementation::MainView>
    {
    };
}
