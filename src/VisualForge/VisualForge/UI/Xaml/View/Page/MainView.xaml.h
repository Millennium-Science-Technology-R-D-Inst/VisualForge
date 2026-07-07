#pragma once

#include "IDE/Shell/FloatingWindowHost.h"
#include "ViewModels/MainViewModel.h"
#include "UI/Xaml/View/Page/MainView.g.h"

namespace winrt::VisualForge::UI::Xaml::View::Page::implementation
{
    struct MainView : MainViewT<MainView>
    {
        MainView();
        void Page_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
		void InvertAppThemeButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
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

        VisualForge::ViewModels::MainViewModel m_viewModel{ nullptr };
        bool m_canGoBack{ false };
        bool m_canGoForward{ false };
        bool m_isDockDragPending{ false };
        bool m_isDockDragFloating{ false };
        ::VisualForge::IDE::Shell::FloatingWindowHost m_floatingWindowHost;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, bool>> m_canGoBackChanged;
		winrt::event_token m_canGoBackChangedToken{};
    };
}

namespace winrt::VisualForge::UI::Xaml::View::Page::factory_implementation
{
    struct MainView : MainViewT<MainView, implementation::MainView>
    {
    };
}
