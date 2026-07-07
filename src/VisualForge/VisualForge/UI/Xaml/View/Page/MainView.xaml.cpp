#include "pch.h"
#include "MainView.xaml.h"
#include "ViewModels/MainViewModel.h"
#include <winrt/Microsoft.UI.Input.h>
#if __has_include("UI/Xaml/View/Page/MainView.g.cpp")
#include "UI/Xaml/View/Page/MainView.g.cpp"
#endif

#include <iostream>
#include <algorithm>
#include "App.xaml.h"
#include "Helpers/WindowHelper.h"
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

import Core.AppSettingsDatabase;

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Windowing;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
using namespace Helpers::WinUIWindowHelper;

namespace winrt::VisualForge::UI::Xaml::View::Page::implementation
{
	MainView::MainView()
	{
		m_viewModel = make<winrt::VisualForge::ViewModels::implementation::MainViewModel>();
		DataContext(m_viewModel);

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

	void MainView::Page_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e)
	{
		RefreshStatusBar();

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
		this->ProblemListSummaryText().Text(m_viewModel.OutputPreview());
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
