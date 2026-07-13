#include "pch.h"
#include "MainView.xaml.h"
#include "ViewModels/MainViewModel.h"
#include <winrt/Microsoft.UI.Input.h>
#if __has_include("UI/Xaml/View/Page/MainView.g.cpp")
#include "UI/Xaml/View/Page/MainView.g.cpp"
#endif

#include <iostream>
#include <algorithm>
#include <cctype>
#include "App.xaml.h"
#include "Helpers/WindowHelper.h"
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <shobjidl_core.h>

#include "Integration/MSBuildAdapter.h"
#include "Integration/GitAdapter.h"
#include "Tool/ProcessSession.h"
#include "UI/Xaml/View/Control/SolutionExplorerControl.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <shellapi.h>
#include <sstream>
#include <thread>

import Core.AppSettingsDatabase;

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Windowing;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace Helpers::WinUIWindowHelper;

namespace winrt::VisualForge::UI::Xaml::View::Page::implementation
{
	namespace
	{
		std::wstring ToFileUri(std::filesystem::path const& path)
		{
			return L"file:///" + path.generic_wstring();
		}

		std::wstring LanguageIdForPath(std::filesystem::path const& path)
		{
			auto extension = path.extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value)
			{
				return static_cast<wchar_t>(std::towlower(value));
			});

			if (extension == L".cpp" || extension == L".cxx" || extension == L".cc" || extension == L".h" || extension == L".hpp")
			{
				return L"cpp";
			}
			if (extension == L".xaml")
			{
				return L"xml";
			}
			return L"plaintext";
		}

		std::filesystem::path PathFromFileUri(std::wstring uri)
		{
			constexpr std::wstring_view Prefix{ L"file:///" };
			if (uri.rfind(Prefix, 0) == 0)
			{
				uri.erase(0, Prefix.size());
			}
			std::replace(uri.begin(), uri.end(), L'/', L'\\');
			return std::filesystem::path{ uri };
		}
	}

	MainView::MainView()
	{
		m_viewModel = make<winrt::VisualForge::ViewModels::implementation::MainViewModel>();

		// Listen for back-button state changes from MainView
		m_canGoBackChangedToken = CanGoBackChanged([this](IInspectable const&, bool canGoBack)
																	 {
																		 try
																		 {
																			 AppTitleBar().IsBackButtonVisible(canGoBack);
																		 }
																		 catch (...)
																		 {
																		 }
																	 });
	}

	MainView::~MainView()
	{
		if (m_languageServiceTimer)
		{
			m_languageServiceTimer.Stop();
		}
		m_languageService.Stop();
		m_debugAdapter.Stop();
		m_terminalSession.Stop();
	}

	void MainView::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		if (!m_isPageInitialized)
		{
			m_isPageInitialized = true;
			DataContext(m_viewModel);
			m_solutionFileOpenToken = SolutionExplorer().FileOpenRequested([this](IInspectable const&, hstring const& path)
			{
				OpenPathAsync(std::filesystem::path{ path.c_str() });
			});
		}

		RefreshStatusBar();
		LoadWorkspaceLayouts();
		StartTerminalSession();

		auto window = winrt::VisualForge::implementation::App::window;
		window.ExtendsContentIntoTitleBar(true);
		if (auto appWindow = window.AppWindow())
		{
			appWindow.TitleBar().PreferredHeightOption(winrt::Microsoft::UI::Windowing::TitleBarHeightOption::Standard);
			window.SetTitleBar(AppTitleBar());
			PlacementRestoration::Enable(window);
#ifdef _DEBUG
			{
				AppTitleBar().Subtitle(L"Dev");
			}
#endif
		}
	}

	void MainView::InvertAppThemeButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		this->RequestedTheme(this->RequestedTheme() == ElementTheme::Dark ? ElementTheme::Light : ElementTheme::Dark);
	}

	void MainView::OpenFile_Click(IInspectable const&, RoutedEventArgs const&)
	{
		OpenFileAsync();
	}

	void MainView::NewFile_Click(IInspectable const&, RoutedEventArgs const&)
	{
		NewFileAsync();
	}

	void MainView::OpenWorkspace_Click(IInspectable const&, RoutedEventArgs const&)
	{
		OpenWorkspaceAsync();
	}

	void MainView::SaveFile_Click(IInspectable const&, RoutedEventArgs const&)
	{
		SaveFileAsync();
	}

	void MainView::SaveAs_Click(IInspectable const&, RoutedEventArgs const&)
	{
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (selected)
		{
			SaveDocumentAsAsync(selected);
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainView::NewFileAsync()
	{
		auto& document = m_documentManager.NewUntitled();
		auto const documentKey = document.Path();
		auto const key = documentKey.wstring();
		m_isLoadingDocument = true;
		auto tab = TabViewItem{};
		auto editor = winrt::VisualForge::UI::Xaml::View::Control::EditorDocumentControl{ nullptr };
		if (!m_firstDocumentTabInUse)
		{
			tab = ActiveDocumentTab();
			editor = MainEditorControl();
			m_firstDocumentTabInUse = true;
		}
		else
		{
			tab.IsClosable(true);
			tab.Content(editor);
			EditorTabs().TabItems().Append(tab);
		}

		auto const tabKey = winrt::get_abi(tab);
		m_documentTabs.emplace(tabKey, DocumentTabState{ nullptr, tab, editor, {}, documentKey, {} });
		editor.TextChanged([this, tabKey](IInspectable const&, IInspectable const&)
		{
			if (!m_isLoadingDocument)
			{
				SynchronizeDocument(tabKey);
			}
		});
		editor.SelectionChanged([this](IInspectable const&, IInspectable const&)
		{
			UpdateEditorPosition();
		});
		editor.CompletionRequested([this, tabKey](IInspectable const&, IInspectable const&)
		{
			RequestCompletionForDocument(tabKey);
		});
		editor.BreakpointRequested([this, tabKey](IInspectable const&, int32_t line)
		{
			ToggleBreakpointForDocument(tabKey, line);
		});
		editor.SetText(L"");
		m_isLoadingDocument = false;
		UpdateDocumentHeader(tabKey);
		EditorTabs().SelectedItem(tab);
		UpdateEditorPosition();
		SetOutput(L"Editor", L"Created " + key);
		ShellStatusText().Text(L"New file");
		co_return;
	}

	void MainView::SaveAll_Click(IInspectable const&, RoutedEventArgs const&)
	{
		SaveAllDocumentsAsync();
	}

	void MainView::BuildProject_Click(IInspectable const&, RoutedEventArgs const&)
	{
		BuildProjectAsync();
	}

	void MainView::RunProject_Click(IInspectable const&, RoutedEventArgs const&)
	{
		ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Debugging, true);
		auto const solution = FindWorkspaceFile(L".slnx");
		auto executable = solution.parent_path() / L"x64" / L"Debug" / L"VisualForge" / L"VisualForge.exe";
		if (solution.empty() || !std::filesystem::exists(executable))
		{
			SetOutput(L"Debug", L"Build the workspace before starting a local executable.");
			return;
		}

		if (m_debugAdapter.Start(L"lldb-dap.exe", solution.parent_path()))
		{
			m_debugAdapter.Launch(executable.wstring(), L"[]", solution.parent_path().wstring(), true);
			m_debugAdapter.ConfigurationDone();
			for (auto const& [path, lines] : m_breakpoints)
			{
				m_debugAdapter.SetBreakpoints(path, lines);
			}
			if (m_viewModel)
			{
				winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkDebugStarting();
			}
			DebugSessionText().Text(L"LLDB-DAP session started; waiting for debugger events.");
			DebugEventsList().Items().Clear();
			auto event = ListViewItem{};
			event.Content(box_value(hstring{ L"Launch request sent: VisualForge.exe" }));
			DebugEventsList().Items().Append(event);
			SetOutput(L"Debug", L"LLDB-DAP launched " + executable.wstring());
			ShellStatusText().Text(L"Running");
		}
		else
		{
			// Keep the prototype useful on machines without lldb-dap.
			auto const result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, solution.parent_path().c_str(), SW_SHOWNORMAL));
			if (result > 32)
			{
				SetOutput(L"Debug", L"lldb-dap unavailable; started target without debugger.");
				DebugSessionText().Text(L"Target started without debugger adapter.");
			}
			else
			{
				SetOutput(L"Debug", L"Unable to start " + executable.wstring());
			}
		}
	}

	void MainView::ContinueDebug_Click(IInspectable const&, RoutedEventArgs const&)
	{
		m_debugAdapter.Continue(1);
		SetOutput(L"DAP", L"Continue requested.");
	}

	void MainView::StepOverDebug_Click(IInspectable const&, RoutedEventArgs const&)
	{
		m_debugAdapter.Next(1);
		SetOutput(L"DAP", L"Step over requested.");
	}

	void MainView::StepInDebug_Click(IInspectable const&, RoutedEventArgs const&)
	{
		m_debugAdapter.StepIn(1);
		SetOutput(L"DAP", L"Step in requested.");
	}

	void MainView::StepOutDebug_Click(IInspectable const&, RoutedEventArgs const&)
	{
		m_debugAdapter.StepOut(1);
		SetOutput(L"DAP", L"Step out requested.");
	}

	void MainView::StopDebug_Click(IInspectable const&, RoutedEventArgs const&)
	{
		m_debugAdapter.Stop();
		for (auto& [_, state] : m_documentTabs)
		{
			state.Editor.SetDebugLine(0);
		}
		if (m_viewModel)
		{
			winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkDebugStopped();
		}
		DebugSessionText().Text(L"Debug session stopped.");
		SetOutput(L"Debug", L"Debug session stopped.");
		RefreshStatusBar();
	}

	void MainView::ClearOutput_Click(IInspectable const&, RoutedEventArgs const&)
	{
		OutputTextBox().Text(L"");
		SetOutput(L"Output", L"Output cleared.");
	}

	void MainView::StartTerminalSession()
	{
		if (m_terminalSession.IsRunning())
		{
			return;
		}

		::VisualForge::Tool::ToolCommand command;
		command.Kind = ::VisualForge::Tool::ToolKind::Compiler;
		command.Executable = L"powershell.exe";
		command.Arguments = { L"-NoLogo", L"-NoProfile", L"-NoExit" };
		command.WorkingDirectory = m_workspaceRoot.empty() ? std::filesystem::current_path() : m_workspaceRoot;
		command.DisplayName = L"VisualForge Developer PowerShell";
		m_terminalOutputBytes = 0;
		if (!m_terminalSession.Start(command))
		{
			TerminalOutputTextBox().Text(L"Unable to start powershell.exe.");
			return;
		}

		TerminalOutputTextBox().Text(L"VisualForge Developer PowerShell\r\n");
		if (!m_languageServiceTimer)
		{
			auto weakThis = get_weak();
			m_languageServiceTimer = DispatcherQueue().CreateTimer();
			m_languageServiceTimer.Interval(Windows::Foundation::TimeSpan{ 1000000 });
			m_languageServiceTimer.Tick([weakThis](IInspectable const&, IInspectable const&)
			{
				if (auto strongThis = weakThis.get())
				{
					strongThis->PollLanguageService();
					strongThis->PollDebugAdapter();
					strongThis->PollTerminal();
				}
			});
			m_languageServiceTimer.Start();
		}
	}

	void MainView::PollTerminal()
	{
		if (!m_terminalSession.IsRunning() && m_terminalSession.State() != ::VisualForge::Tool::ProcessSessionState::Exited)
		{
			return;
		}

		auto snapshot = m_terminalSession.Snapshot();
		auto combined = snapshot.StdOut + snapshot.StdErr;
		if (combined.size() <= m_terminalOutputBytes)
		{
			return;
		}

		auto const unread = combined.substr(m_terminalOutputBytes);
		m_terminalOutputBytes = combined.size();
		auto text = std::wstring{ unread.begin(), unread.end() };
		auto current = std::wstring{ TerminalOutputTextBox().Text() };
		current += text;
		if (current.size() > 30000)
		{
			current.erase(0, current.size() - 30000);
		}
		TerminalOutputTextBox().Text(hstring{ current });
	}

	void MainView::ExecuteTerminal_Click(IInspectable const&, RoutedEventArgs const&)
	{
		auto const command = std::wstring{ TerminalCommandBox().Text() };
		if (command.empty())
		{
			return;
		}
		StartTerminalSession();
		if (!m_terminalSession.IsRunning())
		{
			return;
		}

		auto const line = winrt::to_string(hstring{ command }) + "\r\n";
		if (m_terminalSession.Write(line))
		{
			auto current = std::wstring{ TerminalOutputTextBox().Text() };
			current += L"> " + command + L"\r\n";
			TerminalOutputTextBox().Text(hstring{ current });
			TerminalCommandBox().Text(L"");
		}
	}

	void MainView::WorkspaceModeComboBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		if (m_isApplyingWorkspaceMode)
		{
			return;
		}

		auto const mode = WorkspaceModeComboBox().SelectedIndex() == 1
			? ::VisualForge::IDE::Shell::WorkspaceMode::Debugging
			: ::VisualForge::IDE::Shell::WorkspaceMode::Editing;
		ApplyWorkspaceMode(mode, true);
	}

	void MainView::EditingLayout_Click(IInspectable const&, RoutedEventArgs const&)
	{
		ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Editing, true);
	}

	void MainView::DebuggingLayout_Click(IInspectable const&, RoutedEventArgs const&)
	{
		ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Debugging, true);
	}

	void MainView::StartDebuggingKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Debugging, true);
		RunProject_Click(*this, RoutedEventArgs{});
	}

	winrt::Windows::Foundation::IAsyncAction MainView::OpenFileAsync()
	{
		try
		{
			FileOpenPicker picker;
			picker.ViewMode(PickerViewMode::List);
			picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
			picker.FileTypeFilter().Append(L"*");
			auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
			winrt::check_hresult(initializeWithWindow->Initialize(WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
			auto file = co_await picker.PickSingleFileAsync();
			if (!file)
			{
				co_return;
			}

			co_await OpenStorageFileAsync(file);
		}
		catch (winrt::hresult_error const& error)
		{
			SetOutput(L"Editor", L"Open failed: " + std::wstring{ error.message() });
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainView::OpenWorkspaceAsync()
	{
		try
		{
			FileOpenPicker picker;
			picker.ViewMode(PickerViewMode::List);
			picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
			picker.FileTypeFilter().Append(L".slnx");
			picker.FileTypeFilter().Append(L".sln");
			auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
			winrt::check_hresult(initializeWithWindow->Initialize(WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
			auto solution = co_await picker.PickSingleFileAsync();
			if (!solution)
			{
				co_return;
			}

			m_solutionPath = solution.Path().c_str();
			m_workspaceRoot = m_solutionPath.parent_path();
			PopulateWorkspaceTree();
			StartLanguageService();
			SetOutput(L"Workspace", L"Opened " + m_solutionPath.wstring());
			ShellStatusText().Text(L"Workspace loaded");
			RefreshGitChangesAsync();
		}
		catch (winrt::hresult_error const& error)
		{
			SetOutput(L"Workspace", L"Open failed: " + std::wstring{ error.message() });
		}
	}

	void MainView::StartLanguageService()
	{
		if (m_workspaceRoot.empty())
		{
			return;
		}

		m_languageService.Start(L"clangd.exe", m_workspaceRoot, m_workspaceRoot);
		if (!m_languageServiceTimer)
		{
			auto weakThis = get_weak();
			m_languageServiceTimer = DispatcherQueue().CreateTimer();
			m_languageServiceTimer.Interval(Windows::Foundation::TimeSpan{ 1000000 });
			m_languageServiceTimer.Tick([weakThis](IInspectable const&, IInspectable const&)
			{
				if (auto strongThis = weakThis.get())
				{
					strongThis->PollLanguageService();
					strongThis->PollDebugAdapter();
					strongThis->PollTerminal();
				}
			});
			m_languageServiceTimer.Start();
		}

		if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
		{
			LanguageServiceStatusText().Text(L"clangd: running");
			SetOutput(L"clangd", L"Language server started.");
		}
		else
		{
			LanguageServiceStatusText().Text(L"clangd: unavailable");
			SetOutput(L"clangd", L"clangd.exe could not be started.");
		}
	}

	void MainView::PollDebugAdapter()
	{
		if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
		{
			return;
		}

		auto messages = m_debugAdapter.DrainProtocolMessages();
		if (messages.empty())
		{
			return;
		}

		if (m_viewModel)
		{
			winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().ApplyDapMessages(messages);
		}
		for (auto const& message : messages)
		{
			std::wstring label;
			if (!message.Summary.Event.empty())
			{
				label = L"event: " + message.Summary.Event;
			}
			else if (!message.Summary.Command.empty())
			{
				label = L"response: " + message.Summary.Command;
			}
			if (!label.empty())
			{
				auto item = ListViewItem{};
				item.Content(box_value(hstring{ label }));
				DebugEventsList().Items().Append(item);
			}
			if (message.Summary.Event == L"stopped")
			{
				m_debugAdapter.RequestStackTrace(1);
			}
			if (message.Summary.Event == L"continued")
			{
				for (auto& [_, state] : m_documentTabs)
				{
					state.Editor.SetDebugLine(0);
				}
			}
			if (!message.StackFrames.empty())
			{
				CallStackList().Items().Clear();
				m_stackFrames.clear();
				for (auto const& frame : message.StackFrames)
				{
					auto item = ListViewItem{};
					item.Content(box_value(hstring{
						frame.Name + L"  [" + std::to_wstring(frame.Line) + L"]" }));
					m_stackFrames.emplace(winrt::get_abi(item), frame);
					CallStackList().Items().Append(item);
					for (auto& [_, state] : m_documentTabs)
					{
						if (state.Path == std::filesystem::path{ frame.SourcePath })
						{
							state.Editor.SetDebugLine(frame.Line);
						}
					}
				}
				m_debugAdapter.RequestScopes(message.StackFrames.front().Id);
			}
			if (!message.Scopes.empty())
			{
				for (auto const& scope : message.Scopes)
				{
					if (scope.VariablesReference != 0)
					{
						m_debugAdapter.RequestVariables(scope.VariablesReference);
						break;
					}
				}
			}
			if (!message.Variables.empty())
			{
				LocalsList().Items().Clear();
				for (auto const& variable : message.Variables)
				{
					auto display = variable.Name + L" = " + variable.Value;
					if (!variable.Type.empty())
					{
						display += L"  (" + variable.Type + L")";
					}
					auto item = ListViewItem{};
					item.Content(box_value(hstring{ display }));
					LocalsList().Items().Append(item);
				}
			}
		}
		if (m_viewModel)
		{
			DebugSessionText().Text(m_viewModel.DebugStatus());
		}
		SetOutput(L"DAP", L"Debugger protocol events received.");
		RefreshStatusBar();
	}

	void MainView::PollLanguageService()
	{
		if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
		{
			return;
		}

		for (auto const& message : m_languageService.DrainProtocolMessages())
		{
			if (message.Summary.Method == L"textDocument/publishDiagnostics")
			{
				DiagnosticsList().Items().Clear();
				m_diagnosticEntries.clear();
				std::size_t errors{};
				std::size_t warnings{};
				for (auto const& diagnostic : message.Diagnostics)
				{
					auto item = ListViewItem{};
					auto const isError = diagnostic.Severity == ::VisualForge::EditorCore::LSP::LspDiagnosticSeverity::Error;
					auto const severity = isError ? L"error"
						: diagnostic.Severity == ::VisualForge::EditorCore::LSP::LspDiagnosticSeverity::Warning ? L"warning" : L"info";
					if (isError)
					{
						++errors;
					}
					else if (diagnostic.Severity == ::VisualForge::EditorCore::LSP::LspDiagnosticSeverity::Warning)
					{
						++warnings;
					}
					DiagnosticEntry entry{
						PathFromFileUri(diagnostic.Uri),
						diagnostic.Line + 1,
						diagnostic.Column + 1,
						std::wstring{ severity },
						diagnostic.Code,
						diagnostic.Message
					};
					auto const itemKey = winrt::get_abi(item);
					m_diagnosticEntries.emplace(itemKey, entry);
					auto const displaySeverity = diagnostic.Severity == ::VisualForge::EditorCore::LSP::LspDiagnosticSeverity::Error
						? L"Error"
						: diagnostic.Severity == ::VisualForge::EditorCore::LSP::LspDiagnosticSeverity::Warning ? L"Warning" : L"Info";
					item.Content(box_value(hstring{
						entry.Path.filename().wstring()
						+ L"(" + std::to_wstring(entry.Line) + L"," + std::to_wstring(entry.Column) + L") "
						+ std::wstring{ displaySeverity } + (entry.Code.empty() ? L"" : L" [" + entry.Code + L"]")
						+ L": " + entry.Message }));
					DiagnosticsList().Items().Append(item);
				}

				ProblemStatusText().Text(hstring{
					std::to_wstring(errors) + L" Errors, " + std::to_wstring(warnings) + L" Warnings" });
				SetOutput(L"clangd", L"Diagnostics updated.");
			}
			else if (message.Summary.Method.empty() && message.Payload.find("\"items\"") != std::string::npos)
			{
				CompletionListView().Items().Clear();
				m_completionEntries.clear();
				for (auto const& completion : message.Completions)
				{
					auto item = ListViewItem{};
					item.Content(box_value(hstring{
						completion.Label + (completion.Detail.empty() ? L"" : L"  " + completion.Detail) }));
					m_completionEntries.emplace(
						winrt::get_abi(item),
						CompletionEntry{ completion.Label, completion.Detail, completion.InsertText });
					CompletionListView().Items().Append(item);
				}
				CompletionPopup().IsOpen(!message.Completions.empty());
			}
			else if (message.Summary.Id && *message.Summary.Id == m_pendingHoverRequestId)
			{
				m_pendingHoverRequestId = -1;
				HoverTextBlock().Text(hstring{ message.HoverText.empty() ? L"No hover information." : message.HoverText });
				HoverPopup().IsOpen(true);
			}
			else if (message.Summary.Id && *message.Summary.Id == m_pendingDefinitionRequestId)
			{
				m_pendingDefinitionRequestId = -1;
				if (message.Locations.empty())
				{
					SetOutput(L"clangd", L"Definition not found.");
				}
				else
				{
					auto const& location = message.Locations.front();
					OpenDiagnosticAsync(
						PathFromFileUri(location.Uri),
						location.Line + 1,
						location.Column + 1);
				}
			}
		}
	}

	void MainView::RequestCompletionForDocument(void* key)
	{
		if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
		{
			SetOutput(L"clangd", L"Language service is not running.");
			return;
		}

		auto const found = m_documentTabs.find(key);
		if (found == m_documentTabs.end() || found->second.Path.empty())
		{
			return;
		}

		auto const text = std::wstring{ found->second.Editor.Text() };
		auto const offset = static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart()));
		auto const boundedOffset = (std::min)(offset, text.size());
		auto const line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(boundedOffset), L'\n'));
		auto const lineStart = boundedOffset == 0 ? std::wstring::npos : text.rfind(L'\n', boundedOffset - 1);
		auto const column = boundedOffset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
		m_languageService.RequestCompletion(ToFileUri(found->second.Path), line, column);
	}

	void MainView::RequestHoverForActiveDocument()
	{
		auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!active)
		{
			return;
		}
		auto const found = m_documentTabs.find(winrt::get_abi(active));
		if (found == m_documentTabs.end() || found->second.Path.empty())
		{
			return;
		}
		auto const text = std::wstring{ found->second.Editor.Text() };
		auto const offset = (std::min)(
			static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())),
			text.size());
		auto const line = static_cast<std::size_t>(std::count(
			text.begin(),
			text.begin() + static_cast<std::ptrdiff_t>(offset),
			L'\n'));
		auto const lineStart = offset == 0 ? std::wstring::npos : text.rfind(L'\n', offset - 1);
		auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
		m_pendingHoverRequestId = m_languageService.RequestHover(ToFileUri(found->second.Path), line, column);
		SetOutput(L"clangd", L"Hover requested.");
	}

	void MainView::RequestDefinitionForActiveDocument()
	{
		auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!active)
		{
			return;
		}
		auto const found = m_documentTabs.find(winrt::get_abi(active));
		if (found == m_documentTabs.end() || found->second.Path.empty())
		{
			return;
		}
		auto const text = std::wstring{ found->second.Editor.Text() };
		auto const offset = (std::min)(
			static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())),
			text.size());
		auto const line = static_cast<std::size_t>(std::count(
			text.begin(),
			text.begin() + static_cast<std::ptrdiff_t>(offset),
			L'\n'));
		auto const lineStart = offset == 0 ? std::wstring::npos : text.rfind(L'\n', offset - 1);
		auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
		m_pendingDefinitionRequestId = m_languageService.RequestDefinition(ToFileUri(found->second.Path), line, column);
		SetOutput(L"clangd", L"Go to definition requested.");
	}

	void MainView::HoverKeyboardAccelerator_Invoked(
		Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
		Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
		{
			SetOutput(L"clangd", L"Language service is not running.");
			return;
		}
		RequestHoverForActiveDocument();
	}

	void MainView::GoToDefinitionKeyboardAccelerator_Invoked(
		Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
		Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
		{
			SetOutput(L"clangd", L"Language service is not running.");
			return;
		}
		RequestDefinitionForActiveDocument();
	}

	void MainView::ToggleBreakpointKeyboardAccelerator_Invoked(
		Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
		Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!active)
		{
			return;
		}
		auto const found = m_documentTabs.find(winrt::get_abi(active));
		if (found == m_documentTabs.end() || found->second.Path.empty())
		{
			return;
		}

		auto const text = std::wstring{ found->second.Editor.Text() };
		auto const offset = (std::min)(
			static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())),
			text.size());
		auto const line = static_cast<int>(std::count(
			text.begin(),
			text.begin() + static_cast<std::ptrdiff_t>(offset),
			L'\n')) + 1;
		ToggleBreakpointForDocument(winrt::get_abi(active), line);
	}

	void MainView::ToggleBreakpointForDocument(void* key, int32_t line)
	{
		auto const found = m_documentTabs.find(key);
		if (found == m_documentTabs.end() || found->second.Path.empty() || line <= 0)
		{
			return;
		}

		auto const breakpointKey = found->second.Path.wstring();
		auto& lines = m_breakpoints[breakpointKey];
		auto const existing = std::find(lines.begin(), lines.end(), line);
		auto const enabled = existing == lines.end();
		if (enabled)
		{
			lines.push_back(line);
			SetOutput(L"Debug", L"Breakpoint added at line " + std::to_wstring(line) + L".");
		}
		else
		{
			lines.erase(existing);
			SetOutput(L"Debug", L"Breakpoint removed from line " + std::to_wstring(line) + L".");
		}
		found->second.Editor.SetBreakpoint(line, enabled);
		if (m_debugAdapter.State() == ::VisualForge::EditorCore::DAP::DapClientState::Running)
		{
			m_debugAdapter.SetBreakpoints(breakpointKey, lines);
		}
	}

	void MainView::CallStackList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		auto selected = CallStackList().SelectedItem().try_as<ListViewItem>();
		if (!selected)
		{
			return;
		}
		auto const found = m_stackFrames.find(winrt::get_abi(selected));
		if (found == m_stackFrames.end() || found->second.SourcePath.empty())
		{
			return;
		}
		OpenDiagnosticAsync(
			std::filesystem::path{ found->second.SourcePath },
			static_cast<std::size_t>((std::max)(1, found->second.Line)),
			static_cast<std::size_t>((std::max)(1, found->second.Column)));
	}

	void MainView::CompletionListView_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		auto selected = CompletionListView().SelectedItem().try_as<ListViewItem>();
		if (!selected)
		{
			return;
		}

		auto const found = m_completionEntries.find(winrt::get_abi(selected));
		auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (found == m_completionEntries.end() || !active)
		{
			return;
		}

		auto const document = m_documentTabs.find(winrt::get_abi(active));
		if (document != m_documentTabs.end())
		{
			document->second.Editor.ReplaceSelection(hstring{ found->second.InsertText });
			document->second.Editor.FocusEditor();
		}
		CompletionPopup().IsOpen(false);
	}

	winrt::Windows::Foundation::IAsyncAction MainView::OpenPathAsync(std::filesystem::path const& path)
	{
		try
		{
			auto file = co_await StorageFile::GetFileFromPathAsync(path.c_str());
			co_await OpenStorageFileAsync(file);
		}
		catch (winrt::hresult_error const& error)
		{
			SetOutput(L"Editor", L"Open failed: " + std::wstring{ error.message() });
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainView::OpenStorageFileAsync(StorageFile const& file)
	{
		auto const path = std::filesystem::path{ file.Path().c_str() };
		for (auto const& [key, state] : m_documentTabs)
		{
			if (state.Path == path)
			{
				EditorTabs().SelectedItem(state.Tab);
				co_return;
			}
		}

		auto& document = m_documentManager.Open(path);
		auto const text = hstring{ document.Buffer().CreateSnapshot().Text() };
		if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
		{
			m_languageService.DidOpenFile(ToFileUri(path), LanguageIdForPath(path), std::wstring{ text });
		}
		m_isLoadingDocument = true;
		auto tab = TabViewItem{};
		auto editor = winrt::VisualForge::UI::Xaml::View::Control::EditorDocumentControl{ nullptr };
		if (!m_firstDocumentTabInUse)
		{
			tab = ActiveDocumentTab();
			editor = MainEditorControl();
			m_firstDocumentTabInUse = true;
		}
		else
		{
			tab.IsClosable(true);
			tab.Content(editor);
			EditorTabs().TabItems().Append(tab);
		}

		auto const key = winrt::get_abi(tab);
		m_documentTabs.emplace(key, DocumentTabState{ file, tab, editor, path, path, std::wstring{ text } });
		editor.TextChanged([this, key](IInspectable const&, IInspectable const&)
		{
			if (!m_isLoadingDocument)
			{
				SynchronizeDocument(key);
			}
		});
		editor.SelectionChanged([this](IInspectable const&, IInspectable const&)
		{
			UpdateEditorPosition();
		});
		editor.CompletionRequested([this, key](IInspectable const&, IInspectable const&)
		{
			RequestCompletionForDocument(key);
		});
		editor.BreakpointRequested([this, key](IInspectable const&, int32_t line)
		{
			ToggleBreakpointForDocument(key, line);
		});
		editor.SetText(text);
		m_isLoadingDocument = false;
		UpdateDocumentHeader(key);
		EditorTabs().SelectedItem(tab);
		UpdateEditorPosition();
		SetOutput(L"Editor", L"Opened " + path.wstring());
		ShellStatusText().Text(L"Ready");
	}

	winrt::Windows::Foundation::IAsyncAction MainView::SaveFileAsync()
	{
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected || !m_documentTabs.contains(winrt::get_abi(selected)))
		{
			SetOutput(L"Editor", L"Nothing to save. Open a file first.");
			co_return;
		}

		co_await SaveDocumentAsync(selected);
	}

	winrt::Windows::Foundation::IAsyncAction MainView::SaveAllDocumentsAsync()
	{
		std::vector<TabViewItem> dirtyTabs;
		for (auto const& [_, state] : m_documentTabs)
		{
			if (state.IsDirty)
			{
				dirtyTabs.push_back(state.Tab);
			}
		}

		for (auto const& tab : dirtyTabs)
		{
			co_await SaveDocumentAsync(tab);
		}

		if (dirtyTabs.empty())
		{
			SetOutput(L"Editor", L"All open files are already saved.");
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainView::SaveDocumentAsync(TabViewItem const& tab)
	{
		auto const found = m_documentTabs.find(winrt::get_abi(tab));
		if (found == m_documentTabs.end())
		{
			co_return;
		}

		if (found->second.Path.empty())
		{
			co_await SaveDocumentAsAsync(tab);
			co_return;
		}

		try
		{
			SynchronizeDocument(winrt::get_abi(tab));
			if (auto document = m_documentManager.Find(found->second.DocumentKey))
			{
				document->Save();
			}
			found->second.IsDirty = false;
			UpdateDocumentHeader(winrt::get_abi(tab));
			SetOutput(L"Editor", L"Saved " + found->second.Path.wstring());
			ShellStatusText().Text(L"Saved");
		}
		catch (std::exception const& error)
		{
			SetOutput(L"Editor", L"Save failed: " + std::wstring{ winrt::to_hstring(error.what()) });
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainView::SaveDocumentAsAsync(TabViewItem const& tab)
	{
		auto const found = m_documentTabs.find(winrt::get_abi(tab));
		if (found == m_documentTabs.end())
		{
			co_return;
		}

		try
		{
			FileSavePicker picker;
			picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
			auto const suggestedName = found->second.Path.empty() ? std::wstring{ L"Untitled.cpp" } : found->second.Path.filename().wstring();
			picker.SuggestedFileName(hstring{ suggestedName });
			auto extensions = winrt::single_threaded_vector<hstring>();
			extensions.Append(L".cpp");
			extensions.Append(L".h");
			extensions.Append(L".xaml");
			extensions.Append(L".txt");
			picker.FileTypeChoices().Insert(L"Source files", extensions);
			auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
			winrt::check_hresult(initializeWithWindow->Initialize(WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
			auto file = co_await picker.PickSaveFileAsync();
			if (!file)
			{
				co_return;
			}

			auto const path = std::filesystem::path{ file.Path().c_str() };
			for (auto const& [otherKey, other] : m_documentTabs)
			{
				if (otherKey != winrt::get_abi(tab) && other.Path == path)
				{
					SetOutput(L"Editor", L"That file is already open.");
					co_return;
				}
			}

			SynchronizeDocument(winrt::get_abi(tab));
			if (!m_documentManager.SaveAs(found->second.DocumentKey, path))
			{
				SetOutput(L"Editor", L"Save As failed: document is no longer available.");
				co_return;
			}

			found->second.DocumentKey = path;
			found->second.Path = path;
			found->second.File = file;
			found->second.IsDirty = false;
			UpdateDocumentHeader(winrt::get_abi(tab));
			if (!m_workspaceRoot.empty())
			{
				PopulateWorkspaceTree();
				RefreshGitChangesAsync();
			}
			SetOutput(L"Editor", L"Saved " + path.wstring());
			ShellStatusText().Text(L"Saved");
		}
		catch (winrt::hresult_error const& error)
		{
			SetOutput(L"Editor", L"Save As failed: " + std::wstring{ error.message() });
		}
		catch (std::exception const& error)
		{
			SetOutput(L"Editor", L"Save As failed: " + std::wstring{ winrt::to_hstring(error.what()) });
		}
	}

	void MainView::EditorTabs_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			return;
		}

		if (auto const found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
		{
			m_activeFile = found->second.File;
			found->second.Editor.FocusEditor();
			RefreshSearchMatches(true);
			UpdateEditorPosition();
		}
	}

	void MainView::EditorTabs_TabCloseRequested(TabView const&, TabViewTabCloseRequestedEventArgs const& args)
	{
		CloseDocumentAsync(args.Tab());
	}

	void MainView::SaveKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		SaveFileAsync();
	}

	void MainView::NewFileKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		NewFileAsync();
	}

	void MainView::SaveAsKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (selected)
		{
			SaveDocumentAsAsync(selected);
		}
	}

	void MainView::Undo_Click(IInspectable const&, RoutedEventArgs const&)
	{
		UndoActiveDocument();
	}

	void MainView::Redo_Click(IInspectable const&, RoutedEventArgs const&)
	{
		RedoActiveDocument();
	}

	void MainView::GoToLine_Click(IInspectable const&, RoutedEventArgs const&)
	{
		GoToLineAsync();
	}

	void MainView::UndoKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		UndoActiveDocument();
	}

	void MainView::RedoKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		RedoActiveDocument();
	}

	winrt::Windows::Foundation::IAsyncAction MainView::CloseDocumentAsync(TabViewItem const& tab)
	{
		auto const found = m_documentTabs.find(winrt::get_abi(tab));
		if (found == m_documentTabs.end())
		{
			co_return;
		}

		if (!found->second.IsDirty)
		{
			CloseDocument(tab);
			co_return;
		}

		ContentDialog dialog;
		dialog.XamlRoot(XamlRoot());
		dialog.Title(box_value(L"Unsaved changes"));
		dialog.Content(box_value(L"Save changes before closing this document?"));
		dialog.PrimaryButtonText(L"Save");
		dialog.SecondaryButtonText(L"Discard");
		dialog.CloseButtonText(L"Cancel");
		auto const result = co_await dialog.ShowAsync();
		if (result == ContentDialogResult::Primary)
		{
			co_await SaveDocumentAsync(tab);
			CloseDocument(tab);
		}
		else if (result == ContentDialogResult::Secondary)
		{
			CloseDocument(tab);
		}
	}

	void MainView::CloseDocument(TabViewItem const& tab)
	{
		auto const key = winrt::get_abi(tab);
		auto const wasInitialTab = tab == ActiveDocumentTab();
		if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
		{
			(void)m_documentManager.Close(found->second.DocumentKey);
		}
		m_documentTabs.erase(key);
		uint32_t index{};
		if (EditorTabs().TabItems().IndexOf(tab, index))
		{
			EditorTabs().TabItems().RemoveAt(index);
		}
		if (wasInitialTab)
		{
			m_firstDocumentTabInUse = false;
		}
		m_activeFile = nullptr;
		SetOutput(L"Editor", L"Closed document.");
	}

	void MainView::UpdateDocumentHeader(void* key)
	{
		if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
		{
			auto title = found->second.Path.empty() ? found->second.DocumentKey.filename().wstring() : found->second.Path.filename().wstring();
			if (title.rfind(L"untitled:", 0) == 0)
			{
				title = L"Untitled " + title.substr(9);
			}
			if (found->second.IsDirty)
			{
				title = L"* " + title;
			}
			found->second.Tab.Header(box_value(hstring{ title }));
		}
	}

	void MainView::MarkDocumentDirty(void* key)
	{
		if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end() && !found->second.IsDirty)
		{
			found->second.IsDirty = true;
			UpdateDocumentHeader(key);
		}
	}

	void MainView::SynchronizeDocument(void* key)
	{
		auto const found = m_documentTabs.find(key);
		if (found == m_documentTabs.end())
		{
			return;
		}

		auto document = m_documentManager.Find(found->second.DocumentKey);
		if (!document)
		{
			return;
		}

		auto const current = std::wstring{ found->second.Editor.Text() };
		auto const& previous = found->second.LastText;
		if (current == previous)
		{
			return;
		}

		std::size_t prefix{};
		while (prefix < previous.size() && prefix < current.size() && previous[prefix] == current[prefix])
		{
			++prefix;
		}

		std::size_t suffix{};
		while (suffix < previous.size() - prefix && suffix < current.size() - prefix
			&& previous[previous.size() - 1 - suffix] == current[current.size() - 1 - suffix])
		{
			++suffix;
		}

		auto const deletedLength = previous.size() - prefix - suffix;
		if (deletedLength > 0)
		{
			document->Delete(prefix, deletedLength);
		}

		auto const insertedLength = current.size() - prefix - suffix;
		if (insertedLength > 0)
		{
			document->Insert(prefix, current.substr(prefix, insertedLength));
		}

		found->second.LastText = current;
		found->second.IsDirty = document->IsDirty();
		if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running
			&& !found->second.Path.empty())
		{
			m_languageService.DidChange(ToFileUri(found->second.Path), current);
		}
		UpdateDocumentHeader(key);
	}

	void MainView::ReloadEditorFromDocument(void* key)
	{
		auto const found = m_documentTabs.find(key);
		if (found == m_documentTabs.end())
		{
			return;
		}

		auto document = m_documentManager.Find(found->second.DocumentKey);
		if (!document)
		{
			return;
		}

		auto const text = document->Buffer().CreateSnapshot().Text();
		m_isLoadingDocument = true;
			found->second.Editor.SetText(hstring{ text });
		m_isLoadingDocument = false;
		found->second.LastText = text;
		found->second.IsDirty = document->IsDirty();
		UpdateDocumentHeader(key);
	}

	void MainView::UndoActiveDocument()
	{
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			return;
		}

		auto const key = winrt::get_abi(selected);
		if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
		{
			if (auto document = m_documentManager.Find(found->second.DocumentKey); document && document->Undo())
			{
				ReloadEditorFromDocument(key);
			}
		}
	}

	void MainView::RedoActiveDocument()
	{
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			return;
		}

		auto const key = winrt::get_abi(selected);
		if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
		{
			if (auto document = m_documentManager.Find(found->second.DocumentKey); document && document->Redo())
			{
				ReloadEditorFromDocument(key);
			}
		}
	}

	void MainView::FindTextBox_TextChanged(AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
	{
		RefreshSearchMatches(true);
	}

	void MainView::FindOptions_Changed(IInspectable const&, RoutedEventArgs const&)
	{
		RefreshSearchMatches(true);
	}

	void MainView::FindNext_Click(IInspectable const&, RoutedEventArgs const&)
	{
		NavigateSearchMatch(1);
	}

	void MainView::FindPrevious_Click(IInspectable const&, RoutedEventArgs const&)
	{
		NavigateSearchMatch(-1);
	}

	void MainView::Replace_Click(IInspectable const&, RoutedEventArgs const&)
	{
		ReplaceCurrentMatch();
	}

	void MainView::ReplaceAll_Click(IInspectable const&, RoutedEventArgs const&)
	{
		ReplaceAllMatches();
	}

	void MainView::FindInFiles_Click(IInspectable const&, RoutedEventArgs const&)
	{
		SearchWorkspaceAsync();
	}

	void MainView::SearchResultsList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		auto selected = SearchResultsList().SelectedItem().try_as<ListViewItem>();
		if (selected)
		{
			if (auto const found = m_workspaceSearchResults.find(winrt::get_abi(selected)); found != m_workspaceSearchResults.end())
			{
				OpenSearchResultAsync(found->second.Path, found->second.Offset, std::wstring{ FindTextBox().Text() }.size());
			}
		}
	}

	void MainView::DiagnosticsList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		auto selected = DiagnosticsList().SelectedItem().try_as<ListViewItem>();
		if (selected)
		{
			if (auto const found = m_diagnosticEntries.find(winrt::get_abi(selected)); found != m_diagnosticEntries.end())
			{
				OpenDiagnosticAsync(found->second.Path, found->second.Line, found->second.Column);
			}
		}
	}

	void MainView::FindKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		FocusFindControl(false);
	}

	void MainView::ReplaceKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		FocusFindControl(true);
	}

	void MainView::GoToLineKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		GoToLineAsync();
	}

	void MainView::QuickOpenKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
	{
		args.Handled(true);
		QuickOpenBox().Focus(FocusState::Programmatic);
		QuickOpenBox().Text(L"");
	}

	void MainView::QuickOpenBox_TextChanged(AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
	{
		QuickOpenBox().Items().Clear();
		m_quickOpenResults.clear();
		if (m_workspaceRoot.empty())
		{
			return;
		}

		auto query = std::wstring{ QuickOpenBox().Text() };
		std::transform(query.begin(), query.end(), query.begin(), [](wchar_t value)
		{
			return static_cast<wchar_t>(std::towlower(value));
		});

		for (auto const& path : m_workspaceFiles)
		{
			if (m_quickOpenResults.size() >= 40)
			{
				break;
			}

			std::error_code error;
			auto const relative = std::filesystem::relative(path, m_workspaceRoot, error).wstring();
			auto searchable = relative;
			std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](wchar_t value)
			{
				return static_cast<wchar_t>(std::towlower(value));
			});
			if (!query.empty() && searchable.find(query) == std::wstring::npos)
			{
				continue;
			}

			m_quickOpenResults.push_back(path);
			QuickOpenBox().Items().Append(box_value(hstring{ relative }));
		}
	}

	void MainView::QuickOpenBox_QuerySubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
	{
		auto displayName = unbox_value_or<hstring>(args.ChosenSuggestion(), QuickOpenBox().Text());
		if (auto path = FindQuickOpenPath(displayName); !path.empty())
		{
			OpenQuickOpenResultAsync(path);
		}
	}

	void MainView::RefreshSolutionExplorer_Click(IInspectable const&, RoutedEventArgs const&)
	{
		if (m_workspaceRoot.empty())
		{
			SetOutput(L"Workspace", L"Open a workspace before refreshing Solution Explorer.");
			return;
		}

		PopulateWorkspaceTree();
		SetOutput(L"Workspace", L"Solution Explorer refreshed.");
	}

	void MainView::RefreshGitChanges_Click(IInspectable const&, RoutedEventArgs const&)
	{
		RefreshGitChangesAsync();
	}

	void MainView::GitChangesList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		auto selected = GitChangesList().SelectedItem().try_as<ListViewItem>();
		if (selected)
		{
			if (auto const found = m_gitChanges.find(winrt::get_abi(selected)); found != m_gitChanges.end())
			{
				LoadGitDiffAsync(found->second.Path);
			}
		}
	}

	void MainView::RefreshSearchMatches(bool resetSelection)
	{
		m_searchMatches.clear();
		m_searchMatchIndex = 0;
		FindSummaryText().Text(L"");

		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			return;
		}

		auto const found = m_documentTabs.find(winrt::get_abi(selected));
		if (found == m_documentTabs.end())
		{
			return;
		}

		auto document = m_documentManager.Find(found->second.DocumentKey);
		auto const query = std::wstring{ FindTextBox().Text() };
		if (!document || query.empty())
		{
			return;
		}

		::VisualForge::EditorCore::Search::SearchOptions options;
		auto const matchCase = MatchCaseToggle().IsChecked();
		auto const wholeWord = WholeWordToggle().IsChecked();
		options.MatchCase = matchCase ? matchCase.Value() : false;
		options.WholeWord = wholeWord ? wholeWord.Value() : false;
		m_searchMatches = ::VisualForge::EditorCore::Search::SearchEngine::FindAll(document->Buffer(), query, options);
		FindSummaryText().Text(hstring{ std::to_wstring(m_searchMatches.size()) + L" matches" });
		if (resetSelection && !m_searchMatches.empty())
		{
			NavigateSearchMatch(0);
		}
	}

	void MainView::NavigateSearchMatch(int direction)
	{
		if (m_searchMatches.empty())
		{
			return;
		}

		if (direction > 0)
		{
			m_searchMatchIndex = (m_searchMatchIndex + 1) % m_searchMatches.size();
		}
		else if (direction < 0)
		{
			m_searchMatchIndex = (m_searchMatchIndex + m_searchMatches.size() - 1) % m_searchMatches.size();
		}

		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			return;
		}

		if (auto const found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
		{
			auto const& match = m_searchMatches[m_searchMatchIndex];
			found->second.Editor.Select(static_cast<int32_t>(match.Start), static_cast<int32_t>(match.Length));
				found->second.Editor.FocusEditor();
			FindSummaryText().Text(hstring{ std::to_wstring(m_searchMatchIndex + 1) + L" / " + std::to_wstring(m_searchMatches.size()) });
		}
	}

	void MainView::ReplaceCurrentMatch()
	{
		if (m_searchMatches.empty())
		{
			return;
		}

		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			return;
		}

		auto const found = m_documentTabs.find(winrt::get_abi(selected));
		if (found == m_documentTabs.end())
		{
			return;
		}

		auto document = m_documentManager.Find(found->second.DocumentKey);
		if (!document)
		{
			return;
		}

		auto text = std::wstring{ document->Buffer().CreateSnapshot().Text() };
		auto const& match = m_searchMatches[m_searchMatchIndex];
		text.replace(match.Start, match.Length, std::wstring{ ReplaceTextBox().Text() });
			found->second.Editor.SetText(hstring{ text });
		RefreshSearchMatches(true);
	}

	void MainView::ReplaceAllMatches()
	{
		if (m_searchMatches.empty())
		{
			return;
		}

		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			return;
		}

		auto const found = m_documentTabs.find(winrt::get_abi(selected));
		if (found == m_documentTabs.end())
		{
			return;
		}

			auto document = m_documentManager.Find(found->second.DocumentKey);
		if (!document)
		{
			return;
		}

		auto text = std::wstring{ document->Buffer().CreateSnapshot().Text() };
		auto const replacement = std::wstring{ ReplaceTextBox().Text() };
		for (auto iterator = m_searchMatches.rbegin(); iterator != m_searchMatches.rend(); ++iterator)
		{
			text.replace(iterator->Start, iterator->Length, replacement);
		}

		auto const count = m_searchMatches.size();
			found->second.Editor.SetText(hstring{ text });
		SetOutput(L"Search", L"Replaced " + std::to_wstring(count) + L" matches.");
		RefreshSearchMatches(true);
	}

	void MainView::FocusFindControl(bool replacement)
	{
		auto control = replacement
			? ReplaceTextBox().try_as<::winrt::Microsoft::UI::Xaml::Controls::Control>()
			: FindTextBox().try_as<::winrt::Microsoft::UI::Xaml::Controls::Control>();
		if (control)
		{
			control.Focus(FocusState::Programmatic);
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainView::SearchWorkspaceAsync()
	{
		auto const query = std::wstring{ FindTextBox().Text() };
		if (query.empty())
		{
			SetOutput(L"Search", L"Enter text to search the workspace.");
			co_return;
		}

		if (m_workspaceRoot.empty())
		{
			SetOutput(L"Search", L"Open a workspace before searching files.");
			co_return;
		}

		auto const root = m_workspaceRoot;
		auto const queryUtf8 = winrt::to_string(hstring{ query });
		auto const matchCase = MatchCaseToggle().IsChecked();
		auto const wholeWord = WholeWordToggle().IsChecked();
		auto const isMatchCase = matchCase ? matchCase.Value() : false;
		auto const isWholeWord = wholeWord ? wholeWord.Value() : false;
		auto const queue = DispatcherQueue();
		auto weakThis = get_weak();
		ShellStatusText().Text(L"Searching workspace...");
		co_await winrt::resume_background();

		std::vector<WorkspaceSearchResult> results;
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator{ root, std::filesystem::directory_options::skip_permission_denied, error };
		std::filesystem::recursive_directory_iterator const end;
		for (; iterator != end && !error && results.size() < 200; iterator.increment(error))
		{
			auto const& entry = *iterator;
			if (entry.is_directory(error))
			{
				auto const name = entry.path().filename().wstring();
				if (name == L".git" || name == L".vs" || name == L"obj" || name == L"x64" || name == L"vcpkg_installed")
				{
					iterator.disable_recursion_pending();
				}
				continue;
			}

			if (!entry.is_regular_file(error))
			{
				continue;
			}

			auto const extension = entry.path().extension().wstring();
			if (extension != L".cpp" && extension != L".h" && extension != L".ixx" && extension != L".xaml"
				&& extension != L".json" && extension != L".md" && extension != L".props" && extension != L".targets"
				&& extension != L".vcxproj" && extension != L".slnx")
			{
				continue;
			}

			std::ifstream stream{ entry.path(), std::ios::binary };
			if (!stream)
			{
				continue;
			}

			std::string contents{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
			auto searchable = contents;
			auto searchableQuery = queryUtf8;
			if (!isMatchCase)
			{
				auto lower = [](unsigned char value)
				{
					return static_cast<char>(std::tolower(value));
				};
				std::transform(searchable.begin(), searchable.end(), searchable.begin(), lower);
				std::transform(searchableQuery.begin(), searchableQuery.end(), searchableQuery.begin(), lower);
			}

			std::size_t offset{};
			while (results.size() < 200)
			{
				auto const match = searchable.find(searchableQuery, offset);
				if (match == std::string::npos)
				{
					break;
				}
				if (isWholeWord)
				{
					auto const isWord = [](char value)
					{
						return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
					};
					auto const beforeIsWord = match > 0 && isWord(contents[match - 1]);
					auto const after = match + queryUtf8.size();
					auto const afterIsWord = after < contents.size() && isWord(contents[after]);
					if (beforeIsWord || afterIsWord)
					{
						offset = match + (std::max<std::size_t>)(1, queryUtf8.size());
						continue;
					}
				}

				auto const lineStart = contents.rfind('\n', match);
				auto const previewStart = lineStart == std::string::npos ? 0 : lineStart + 1;
				auto const lineEnd = contents.find('\n', match);
				auto preview = contents.substr(previewStart, (lineEnd == std::string::npos ? contents.size() : lineEnd) - previewStart);
				std::replace(preview.begin(), preview.end(), '\t', ' ');
				results.push_back({ entry.path(), match, static_cast<std::size_t>(std::count(contents.begin(), contents.begin() + static_cast<std::ptrdiff_t>(match), '\n')) + 1, match - previewStart + 1, std::wstring{ winrt::to_hstring(preview) } });
				offset = match + (std::max<std::size_t>)(1, queryUtf8.size());
			}
		}

		(void)queue.TryEnqueue([this, weakThis, results = std::move(results)]
		{
			if (!weakThis.get())
			{
				return;
			}
			SearchResultsList().Items().Clear();
			m_workspaceSearchResults.clear();
			for (auto const& result : results)
			{
				auto item = ListViewItem{};
				auto const label = result.Path.filename().wstring() + L":" + std::to_wstring(result.Line) + L"  " + result.Preview;
				item.Content(box_value(hstring{ label }));
				SearchResultsList().Items().Append(item);
				m_workspaceSearchResults.emplace(winrt::get_abi(item), result);
			}

			FindSummaryText().Text(hstring{ std::to_wstring(results.size()) + L" workspace matches" });
			ShellStatusText().Text(results.empty() ? L"No workspace matches" : L"Workspace search complete");
		});
	}

	winrt::Windows::Foundation::IAsyncAction MainView::OpenSearchResultAsync(std::filesystem::path const& path, std::size_t offset, std::size_t length)
	{
		co_await OpenPathAsync(path);
		for (auto const& [key, state] : m_documentTabs)
		{
			if (state.Path == path)
			{
				EditorTabs().SelectedItem(state.Tab);
				state.Editor.Select(static_cast<int32_t>(offset), static_cast<int32_t>(length));
				break;
			}
		}
	}

	winrt::Windows::Foundation::IAsyncAction MainView::OpenQuickOpenResultAsync(std::filesystem::path const& path)
	{
		co_await OpenPathAsync(path);
		QuickOpenBox().Text(L"");
		QuickOpenBox().Focus(FocusState::Programmatic);
	}

	winrt::Windows::Foundation::IAsyncAction MainView::RefreshGitChangesAsync()
	{
		if (m_workspaceRoot.empty())
		{
			GitChangesList().Items().Clear();
			m_gitChanges.clear();
			GitChangeSummaryText().Text(L"No repository loaded");
			GitBranchStatusText().Text(L"git: unavailable");
			co_return;
		}

		auto const root = m_workspaceRoot;
		auto const queue = DispatcherQueue();
		auto weakThis = get_weak();
		GitChangeSummaryText().Text(L"Loading...");
		co_await winrt::resume_background();

		::VisualForge::Integration::GitAdapter adapter;
		auto command = adapter.CreateStatusCommand(root);
		::VisualForge::Tool::ProcessSession process;
		auto const started = process.Start(command);
		if (started)
		{
			(void)process.WaitForExit();
		}
		auto snapshot = process.Snapshot();
		auto output = snapshot.StdOut + snapshot.StdErr;
		if (output.size() > 16000)
		{
			output.erase(0, output.size() - 16000);
		}

		(void)queue.TryEnqueue([this, weakThis, started, exitCode = snapshot.ExitCode, output = std::move(output)]
		{
			if (!weakThis.get())
			{
				return;
			}
			if (!started)
			{
				ParseGitStatus({});
				GitChangeSummaryText().Text(L"Git unavailable");
				GitBranchStatusText().Text(L"git: unavailable");
				GitDiffOutputTextBox().Text(L"Git could not be started.");
				return;
			}

			ParseGitStatus(output);
			if (exitCode != 0)
			{
				GitChangeSummaryText().Text(L"Git command failed");
			}
			ShellStatusText().Text(exitCode == 0 ? L"Git refreshed" : L"Git error");
		});
	}

	winrt::Windows::Foundation::IAsyncAction MainView::LoadGitDiffAsync(std::filesystem::path const& path)
	{
		if (m_workspaceRoot.empty())
		{
			co_return;
		}

		auto const root = m_workspaceRoot;
		auto const queue = DispatcherQueue();
		auto weakThis = get_weak();
		GitDiffOutputTextBox().Text(L"Loading diff...");
		co_await winrt::resume_background();

		std::error_code relativeError;
		auto relativePath = std::filesystem::relative(path, root, relativeError);
		if (relativeError)
		{
			relativePath = path.filename();
		}
		::VisualForge::Integration::GitAdapter adapter;
		auto command = adapter.CreateDiffCommand(root, relativePath);
		::VisualForge::Tool::ProcessSession process;
		auto const started = process.Start(command);
		if (started)
		{
			(void)process.WaitForExit();
		}
		auto snapshot = process.Snapshot();
		auto output = snapshot.StdOut + snapshot.StdErr;
		if (output.size() > 30000)
		{
			output.erase(0, output.size() - 30000);
		}
		(void)queue.TryEnqueue([this, weakThis, started, output = std::move(output)]
		{
			if (!weakThis.get())
			{
				return;
			}
			if (!started)
			{
				GitDiffOutputTextBox().Text(L"Git could not be started.");
				return;
			}

			auto const wideOutput = std::wstring{ winrt::to_hstring(output) };
			if (wideOutput.empty())
			{
				GitDiffOutputTextBox().Text(L"No unstaged diff for this file.");
			}
			else
			{
				GitDiffOutputTextBox().Text(hstring{ wideOutput });
			}
		});
	}

	void MainView::ParseGitStatus(std::string const& output)
	{
		GitChangesList().Items().Clear();
		m_gitChanges.clear();
		std::string branch{ "unavailable" };
		std::size_t count{};
		std::istringstream lines{ output };
		std::string line;
		while (std::getline(lines, line))
		{
			if (line.rfind("## ", 0) == 0)
			{
				branch = line.substr(3);
				continue;
			}
			if (line.size() < 4 || line[0] == '#')
			{
				continue;
			}

			auto const status = line.substr(0, 2);
			auto pathText = line.substr(3);
			if (pathText.empty())
			{
				continue;
			}
			if ((status[0] == 'R' || status[0] == 'C') && pathText.find(" -> ") != std::string::npos)
			{
				pathText = pathText.substr(pathText.rfind(" -> ") + 4);
			}

			auto const path = m_workspaceRoot / std::filesystem::path{ std::wstring{ winrt::to_hstring(pathText) } };
			auto item = ListViewItem{};
			auto const statusWide = std::wstring{ winrt::to_hstring(status) };
			auto const pathWide = std::wstring{ winrt::to_hstring(pathText) };
			item.Content(box_value(hstring{ statusWide + L"  " + pathWide }));
			GitChangesList().Items().Append(item);
			m_gitChanges.emplace(winrt::get_abi(item), GitChangeEntry{ path, status, pathWide });
			++count;
		}

		GitBranchStatusText().Text(hstring{ L"git: " + std::wstring{ winrt::to_hstring(branch) } });
		GitChangeSummaryText().Text(hstring{ std::to_wstring(count) + L" changes" });
	}

	void MainView::LoadWorkspaceLayouts()
	{
		if (!m_viewModel)
		{
			return;
		}

		auto& workspace = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace();
		auto& database = Core::AppSettingsDatabase::Instance();
		auto& editing = workspace.Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing);
		auto& debugging = workspace.Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging);
		editing.BottomToolHeight = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_height", editing.BottomToolHeight)), 220, 520);
		debugging.BottomToolHeight = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_height", debugging.BottomToolHeight)), 260, 520);
		editing.RightToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.right_tab", editing.RightToolTabIndex)), 0, 3);
		debugging.RightToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.right_tab", debugging.RightToolTabIndex)), 0, 3);
		editing.BottomToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_tab", editing.BottomToolTabIndex)), 0, 4);
		debugging.BottomToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_tab", debugging.BottomToolTabIndex)), 0, 4);
		editing.ShowDiagnosticTools = database.GetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.show_diagnostics").value_or(false);
		debugging.ShowDiagnosticTools = database.GetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.show_diagnostics").value_or(true);

		auto const savedMode = database.GetString(Core::AppSettingsDatabase::CAT_UI, "workspace.mode").value_or("editing");
		ApplyWorkspaceMode(savedMode == "debugging"
			? ::VisualForge::IDE::Shell::WorkspaceMode::Debugging
			: ::VisualForge::IDE::Shell::WorkspaceMode::Editing, false);
	}

	void MainView::ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode mode, bool persist)
	{
		if (!m_viewModel)
		{
			return;
		}

		m_isApplyingWorkspaceMode = true;
		auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
		viewModel->Workspace().SetMode(mode);
		auto const& profile = viewModel->Workspace().Profile(mode);
		WorkspaceModeComboBox().SelectedIndex(mode == ::VisualForge::IDE::Shell::WorkspaceMode::Debugging ? 1 : 0);
		EditorAreaRow().Height({ 1, GridUnitType::Star });
		BottomToolRow().Height({ static_cast<double>(profile.BottomToolHeight), GridUnitType::Pixel });
		DebugToolsTab().Visibility(profile.ShowDiagnosticTools ? Visibility::Visible : Visibility::Collapsed);
		CallStackTab().Visibility(profile.ShowDiagnosticTools ? Visibility::Visible : Visibility::Collapsed);
		LocalsTab().Visibility(profile.ShowDiagnosticTools ? Visibility::Visible : Visibility::Collapsed);
		if (m_documentTabs.empty())
		{
			EditorTabs().SelectedItem(ActiveDocumentTab());
		}

		if (mode == ::VisualForge::IDE::Shell::WorkspaceMode::Debugging)
		{
			BottomToolWindowTitleText().Text(L"Debug Tools");
			RightToolTabs().SelectedIndex(profile.RightToolTabIndex);
			BottomToolTabs().SelectedIndex(profile.BottomToolTabIndex);
			DebugSessionText().Text(m_viewModel.DebugStatus());
			DebugEventsList().Items().Clear();
			auto event = ListViewItem{};
			event.Content(box_value(hstring{ L"Debug layout active. Press F5 to start the local target." }));
			DebugEventsList().Items().Append(event);
		}
		else
		{
			BottomToolWindowTitleText().Text(L"Error List - Current Project");
			RightToolTabs().SelectedIndex(profile.RightToolTabIndex);
			BottomToolTabs().SelectedIndex(profile.BottomToolTabIndex);
		}

		m_isApplyingWorkspaceMode = false;
		if (persist)
		{
			auto& database = Core::AppSettingsDatabase::Instance();
			database.SetString(Core::AppSettingsDatabase::CAT_UI, "workspace.mode", mode == ::VisualForge::IDE::Shell::WorkspaceMode::Debugging ? "debugging" : "editing");
			database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_height", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).BottomToolHeight);
			database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_height", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).BottomToolHeight);
			database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.right_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).RightToolTabIndex);
			database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.right_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).RightToolTabIndex);
			database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).BottomToolTabIndex);
			database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).BottomToolTabIndex);
			database.SetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.show_diagnostics", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).ShowDiagnosticTools);
			database.SetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.show_diagnostics", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).ShowDiagnosticTools);
		}
	}

	void MainView::RightToolTabs_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		if (m_isApplyingWorkspaceMode || !m_viewModel)
		{
			return;
		}

		auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
		viewModel->Workspace().Profile(viewModel->Workspace().Mode()).RightToolTabIndex = RightToolTabs().SelectedIndex();
		ApplyWorkspaceMode(viewModel->Workspace().Mode(), true);
	}

	void MainView::BottomToolTabs_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
	{
		if (m_isApplyingWorkspaceMode || !m_viewModel)
		{
			return;
		}

		auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
		viewModel->Workspace().Profile(viewModel->Workspace().Mode()).BottomToolTabIndex = BottomToolTabs().SelectedIndex();
		ApplyWorkspaceMode(viewModel->Workspace().Mode(), true);
	}

	winrt::Windows::Foundation::IAsyncAction MainView::GoToLineAsync()
	{
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			co_return;
		}

		auto const found = m_documentTabs.find(winrt::get_abi(selected));
		if (found == m_documentTabs.end())
		{
			co_return;
		}

		TextBox input;
		input.Width(220);
		input.PlaceholderText(L"Line number");
		input.Text(L"1");
		input.SelectAll();

		ContentDialog dialog;
		dialog.Title(box_value(L"Go to Line"));
		dialog.Content(input);
		dialog.PrimaryButtonText(L"Go");
		dialog.CloseButtonText(L"Cancel");
		dialog.DefaultButton(ContentDialogButton::Primary);
		dialog.XamlRoot(this->XamlRoot());
		if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
		{
			co_return;
		}

		std::size_t requestedLine{};
		try
		{
			requestedLine = static_cast<std::size_t>(std::stoull(std::wstring{ input.Text() }));
		}
		catch (...)
		{
			SetOutput(L"Editor", L"Enter a valid line number.");
			co_return;
		}

		if (requestedLine == 0)
		{
			requestedLine = 1;
		}

		auto const text = std::wstring{ found->second.Editor.Text() };
		std::size_t offset{};
		std::size_t lastLine = 1;
		for (auto position = text.find(L'\n'); position != std::wstring::npos; position = text.find(L'\n', position + 1))
		{
			++lastLine;
		}
		requestedLine = (std::min)(requestedLine, lastLine);
		for (std::size_t currentLine = 1; currentLine < requestedLine; ++currentLine)
		{
			auto const newline = text.find(L'\n', offset);
			if (newline == std::wstring::npos)
			{
				offset = text.size();
				break;
			}
			offset = newline + 1;
		}

		found->second.Editor.Select(static_cast<int32_t>(offset), 0);
			found->second.Editor.FocusEditor();
		UpdateEditorPosition();
	}

	winrt::Windows::Foundation::IAsyncAction MainView::OpenDiagnosticAsync(std::filesystem::path const& path, std::size_t line, std::size_t column)
	{
		co_await OpenPathAsync(path);
		for (auto const& [key, state] : m_documentTabs)
		{
			if (state.Path != path)
			{
				continue;
			}

			EditorTabs().SelectedItem(state.Tab);
			auto const text = std::wstring{ state.Editor.Text() };
			std::size_t offset{};
			for (std::size_t currentLine = 1; currentLine < line && offset < text.size(); ++currentLine)
			{
				auto const newline = text.find(L'\n', offset);
				if (newline == std::wstring::npos)
				{
					offset = text.size();
					break;
				}
				offset = newline + 1;
			}

			if (column > 0)
			{
				offset = (std::min)(offset + column - 1, text.size());
			}
			state.Editor.Select(static_cast<int32_t>(offset), 1);
			state.Editor.FocusEditor();
			break;
		}
	}

	void MainView::ParseBuildDiagnostics(std::wstring const& output)
	{
		DiagnosticsList().Items().Clear();
		m_diagnosticEntries.clear();
		std::size_t errors{};
		std::size_t warnings{};
		std::size_t messages{};
		std::string narrow = winrt::to_string(hstring{ output });
		std::regex pattern{ R"((.+)\((\d+),(\d+)\):\s+(error|warning)\s+([A-Za-z]+\d+):\s*(.*))" };
		std::istringstream lines{ narrow };
		std::string line;
		while (std::getline(lines, line))
		{
			std::smatch match;
			if (!std::regex_match(line, match, pattern))
			{
				continue;
			}

			auto path = std::filesystem::path{ match[1].str() };
			if (path.is_relative())
			{
				path = m_workspaceRoot / path;
			}
			DiagnosticEntry entry{
				path,
				static_cast<std::size_t>(std::stoull(match[2].str())),
				static_cast<std::size_t>(std::stoull(match[3].str())),
				std::wstring{ winrt::to_hstring(match[4].str()) },
				std::wstring{ winrt::to_hstring(match[5].str()) },
				std::wstring{ winrt::to_hstring(match[6].str()) }};
			if (entry.Severity == L"error")
			{
				++errors;
			}
			else
			{
				++warnings;
			}

			auto item = ListViewItem{};
			auto const label = entry.Severity + L" " + entry.Code + L"  " + entry.Path.filename().wstring()
				+ L":" + std::to_wstring(entry.Line) + L":" + std::to_wstring(entry.Column) + L"  " + entry.Message;
			item.Content(box_value(hstring{ label }));
			DiagnosticsList().Items().Append(item);
			m_diagnosticEntries.emplace(winrt::get_abi(item), std::move(entry));
		}

		ErrorCountText().Text(hstring{ std::to_wstring(errors) + L" Errors" });
		WarningCountText().Text(hstring{ std::to_wstring(warnings) + L" Warnings" });
		MessageCountText().Text(hstring{ std::to_wstring(messages) + L" Messages" });
	}

	void MainView::SetOutput(std::wstring const& source, std::wstring const& text)
	{
		if (!text.empty())
		{
			auto current = std::wstring{ OutputTextBox().Text() };
			if (!current.empty() && current.back() != L'\n')
			{
				current += L'\n';
			}
			current += L"[" + source + L"] " + text;
			if (current.size() > 30000)
			{
				current.erase(0, current.size() - 30000);
			}
			OutputTextBox().Text(hstring{ current });
		}
		if (m_viewModel)
		{
			winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().AppendOutput(source, text);
			RefreshStatusBar();
		}
	}

	void MainView::PopulateWorkspaceTree()
	{
		if (m_workspaceRoot.empty())
		{
			return;
		}

		SolutionExplorer().LoadWorkspace(
			hstring{ m_workspaceRoot.wstring() },
			hstring{ m_solutionPath.wstring() });
		m_workspaceFiles.clear();
		std::error_code error;
		for (auto const& entry : std::filesystem::recursive_directory_iterator(m_workspaceRoot, std::filesystem::directory_options::skip_permission_denied, error))
		{
			if (error)
			{
				break;
			}
			if (entry.is_regular_file(error))
			{
				m_workspaceFiles.push_back(entry.path());
			}
		}
	}

	std::filesystem::path MainView::FindQuickOpenPath(std::wstring_view displayName) const
	{
		for (auto const& path : m_quickOpenResults)
		{
			std::error_code error;
			auto const relative = std::filesystem::relative(path, m_workspaceRoot, error).wstring();
			if (relative == displayName || path.filename().wstring() == displayName)
			{
				return path;
			}
		}

		return {};
	}

	void MainView::UpdateEditorPosition()
	{
		auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
		if (!selected)
		{
			EditorPositionText().Text(L"Ln 1, Col 1");
			return;
		}

		auto const found = m_documentTabs.find(winrt::get_abi(selected));
		if (found == m_documentTabs.end())
		{
			EditorPositionText().Text(L"Ln 1, Col 1");
			return;
		}

		auto const text = std::wstring{ found->second.Editor.Text() };
		auto const selectionStart = static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart()));
		auto const offset = (std::min)(selectionStart, text.size());
		auto const lineStart = offset == 0 ? std::wstring::npos : text.rfind(L'\n', offset - 1);
		auto const line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), L'\n')) + 1;
		auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1) + 1;
		EditorPositionText().Text(hstring{ L"Ln " + std::to_wstring(line) + L", Col " + std::to_wstring(column) });
	}

	std::filesystem::path MainView::FindWorkspaceFile(std::wstring_view extension) const
	{
		if (m_workspaceRoot.empty())
		{
			return {};
		}

		if (extension == L".slnx" && !m_solutionPath.empty() && std::filesystem::exists(m_solutionPath))
		{
			return m_solutionPath;
		}

		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator{ m_workspaceRoot, std::filesystem::directory_options::skip_permission_denied, error };
		std::filesystem::recursive_directory_iterator const end;
		for (; iterator != end && !error; iterator.increment(error))
		{
			auto const& entry = *iterator;

			if (entry.is_directory(error))
			{
				auto const name = entry.path().filename().wstring();
				if (name == L".git" || name == L".vs" || name == L"obj" || name == L"x64" || name == L"vcpkg_installed")
				{
					iterator.disable_recursion_pending();
				}
				continue;
			}

			if (entry.is_regular_file(error) && entry.path().extension().wstring() == extension)
			{
				return entry.path();
			}
		}

		return {};
	}

	winrt::Windows::Foundation::IAsyncAction MainView::BuildProjectAsync()
	{
		auto const solution = FindWorkspaceFile(L".slnx");
		if (solution.empty())
		{
			SetOutput(L"Build", L"Open a workspace folder that contains a .slnx file before building.");
			co_return;
		}

		ShellStatusText().Text(L"Building...");
		SetOutput(L"Build", L"Building " + solution.filename().wstring());
		OutputTextBox().Text(L"Building " + solution.filename().wstring() + L"...\n");
		auto const queue = DispatcherQueue();
		auto weakThis = get_weak();
		co_await winrt::resume_background();

		::VisualForge::Integration::ProjectContext context;
		context.WorkspaceRoot = solution.parent_path();
		context.SolutionPath = solution;
		::VisualForge::Integration::MSBuildAdapter adapter;
		auto command = adapter.CreateBuildCommand(context);
		::VisualForge::Tool::ProcessSession process;
		auto const started = process.Start(command);
		while (started && process.IsRunning())
		{
			auto snapshot = process.Snapshot();
			auto output = snapshot.StdOut + snapshot.StdErr;
			if (output.size() > 20000)
			{
				output.erase(0, output.size() - 20000);
			}
			auto wideOutput = std::wstring{ output.begin(), output.end() };
			(void)queue.TryEnqueue([this, weakThis, wideOutput = std::move(wideOutput)]
			{
				if (!weakThis.get())
				{
					return;
				}
				OutputTextBox().Text(hstring{ wideOutput });
			});
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
		if (started)
		{
			(void)process.WaitForExit();
		}
		auto snapshot = process.Snapshot();

		auto output = snapshot.StdOut + snapshot.StdErr;
		if (output.size() > 20000)
		{
			output.erase(0, output.size() - 20000);
		}
		(void)queue.TryEnqueue([this, weakThis, started, exitCode = snapshot.ExitCode, output = std::move(output)]
		{
			if (!weakThis.get())
			{
				return;
			}
			auto const wideOutput = started ? std::wstring{ output.begin(), output.end() } : L"MSBuild could not be started.";
			OutputTextBox().Text(hstring{ wideOutput });
			ParseBuildDiagnostics(wideOutput);
			SetOutput(L"Build", wideOutput);
			ShellStatusText().Text(started && exitCode == 0 ? L"Build succeeded" : L"Build failed");
		});
	}

	Microsoft::UI::Xaml::Visibility MainView::IsDebug()
	{
#ifdef _DEBUG
		return Microsoft::UI::Xaml::Visibility::Visible;
#else
		return Microsoft::UI::Xaml::Visibility::Collapsed;
#endif
	}

	winrt::Windows::Foundation::IAsyncAction MainView::LoadBackground()
	{
		auto& db = Core::AppSettingsDatabase::Instance();
		auto imagePath = db.GetStringW(Core::AppSettingsDatabase::CAT_UI, "background_image").value_or(L"");
		if (imagePath.empty())
		{
			this->Background(nullptr);
			co_return;
		}

		std::wstring imageUri = L"file:///" + imagePath;
		std::replace(imageUri.begin(), imageUri.end(), L'\\', L'/');

		auto const stretchIndex = std::clamp(static_cast<int>(db.GetInt(Core::AppSettingsDatabase::CAT_UI, "image_stretch", 3)), 0, 3);
		auto const opacity = std::clamp(db.GetDouble(Core::AppSettingsDatabase::CAT_UI, "image_opacity").value_or(20.0), 0.0, 100.0) / 100.0;

		auto brush = ImageBrush{};
		auto bitmap = BitmapImage{};
		bitmap.UriSource(winrt::Windows::Foundation::Uri{ imageUri });
		brush.ImageSource(bitmap);
		switch (stretchIndex)
		{
			case 1:
				brush.Stretch(Stretch::Fill);
				break;
			case 2:
				brush.Stretch(Stretch::Uniform);
				break;
			case 3:
				brush.Stretch(Stretch::UniformToFill);
				break;
			case 0:
			default:
				brush.Stretch(Stretch::None);
				break;
		}
		brush.Opacity(opacity);
		this->Background(brush);
		co_return;
	}

	void MainView::RootPage_PointerPressed(winrt::Windows::Foundation::IInspectable const& /*sender*/, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
	{
		auto props = e.GetCurrentPoint(nullptr).Properties();

		if (props.IsXButton1Pressed())
		{
			if (CanGoBack())
			{
				GoBack();
				e.Handled(true);
			}
		}
		else if (props.IsXButton2Pressed())
		{
			if (CanGoForward())
			{
				GoForward();
				e.Handled(true);
			}
		}
	}

	VisualForge::ViewModels::MainViewModel MainView::ViewModel()
	{
		return m_viewModel;
	}

	event_token MainView::CanGoBackChanged(TypedEventHandler<IInspectable, bool> const& handler)
	{
		return m_canGoBackChanged.add(handler);
	}

	void MainView::AppTitleBar_BackRequested(Microsoft::UI::Xaml::Controls::TitleBar const&, IInspectable const&)
	{
		GoBack();
	}

	void MainView::CanGoBackChanged(event_token const& token) noexcept
	{
		m_canGoBackChanged.remove(token);
	}

	void MainView::Navigate(hstring const& tag)
	{
		if (!tag.empty())
		{
			UpdateNavigationState(true, false);
		}
	}

	void MainView::GoBack()
	{
		UpdateNavigationState(false, true);
	}

	void MainView::GoForward()
	{
		UpdateNavigationState(true, false);
	}

	bool MainView::CanGoBack() const noexcept
	{
		return m_canGoBack;
	}

	bool MainView::CanGoForward() const noexcept
	{
		return m_canGoForward;
	}

	void MainView::DockTitleBar_PointerPressed(
		IInspectable const&,
		Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		m_isDockDragPending = true;
		m_isDockDragFloating = false;
		ShowDockFloatingPreview(true);
		args.Handled(true);
	}

	void MainView::DockTitleBar_PointerMoved(
		IInspectable const&,
		Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		if (!m_isDockDragPending)
		{
			return;
		}

		auto const point = args.GetCurrentPoint(*this);
		if (point.Properties().IsLeftButtonPressed())
		{
			m_isDockDragFloating = true;
			ShowDockFloatingPreview(true);
			args.Handled(true);
		}
	}

	void MainView::DockTitleBar_PointerReleased(
		IInspectable const& sender,
		Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		if (m_isDockDragPending)
		{
			m_isDockDragPending = false;
			ShowDockFloatingPreview(false);
			if (m_isDockDragFloating)
			{
				OpenFloatingToolWindow(sender);
			}

			m_isDockDragFloating = false;
			args.Handled(true);
		}
	}

	void MainView::UpdateNavigationState(bool canGoBack, bool canGoForward)
	{
		if (m_canGoBack != canGoBack)
		{
			m_canGoBack = canGoBack;
			m_canGoBackChanged(*this, m_canGoBack);
		}

		m_canGoForward = canGoForward;
	}

	void MainView::RefreshStatusBar()
	{
		if (!m_viewModel)
		{
			return;
		}

		this->ShellStatusText().Text(L"Ready");
		this->LanguageServiceStatusText().Text(m_viewModel.LanguageServiceStatus());
		this->BuildStatusText().Text(m_viewModel.BuildStatus());
		this->DebugStatusText().Text(m_viewModel.DebugStatus());
		this->ProblemStatusText().Text(m_viewModel.ProblemsStatus());
	}

	void MainView::ShowDockFloatingPreview(bool visible)
	{
		this->FloatingPreviewHost().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
	}

	void MainView::OpenFloatingToolWindow(IInspectable const& sender)
	{
		auto const element = sender.try_as<FrameworkElement>();
		if (!element)
		{
			return;
		}

		auto const tag = unbox_value_or<hstring>(element.Tag(), L"");
		if (tag.empty())
		{
			return;
		}

		auto contentId = std::wstring{ tag };
		auto title = std::wstring{ m_viewModel.ToolWindowTitle(tag) };
		auto const floated = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(contentId, ::VisualForge::IDE::Shell::DockCommandKind::Float);
		(void)floated;
		auto const opened = m_floatingWindowHost.Show({ contentId, title, 760, 520, true });
		(void)opened;
		RefreshStatusBar();
	}
}
