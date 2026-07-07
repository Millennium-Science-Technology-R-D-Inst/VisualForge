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

		// ViewModel (delegated to MainContentView)
		VisualForge::ViewModels::MainViewModel ViewModel();
    };
}
namespace winrt::VisualForge::UI::Xaml::View::Window::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
