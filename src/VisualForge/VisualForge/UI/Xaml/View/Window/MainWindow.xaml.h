#pragma once

#include "winrt/VisualForge.UI.Xaml.View.Page.h"
#include "UI/Xaml/View/Window/MainWindow.g.h"
//#include "winrt/ViewModels.h"
#include "ViewModels/MainViewModel.h"

namespace winrt::VisualForge::UI::Xaml::View::Window::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
	public:
		MainWindow();

		void InvertAppThemeButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		// ViewModel (delegated to MainContentView)
		VisualForge::ViewModels::MainViewModel ViewModel();

		// Navigation (delegated to MainContentView)
		void Navigate(winrt::hstring const& tag);

		// Event handlers (XAML wired)
		void AppTitleBar_BackRequested(winrt::Microsoft::UI::Xaml::Controls::TitleBar const&,
									   winrt::Windows::Foundation::IInspectable const&);

		void Grid_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void RootGrid_PointerPressed(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

		winrt::Windows::Foundation::IAsyncAction LoadBackground();

		Microsoft::UI::Xaml::Visibility IsDebug();
	private:
		void InitWindowStyle(winrt::Microsoft::UI::Xaml::Window const& window);
		void RootGridXamlRoot_Changed(winrt::Microsoft::UI::Xaml::XamlRoot sender, winrt::Microsoft::UI::Xaml::XamlRootChangedEventArgs args);
		winrt::event_token m_canGoBackChangedToken{};
    };
}
namespace winrt::VisualForge::UI::Xaml::View::Window::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
