#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("UI/Xaml/View/Window/MainWindow.g.cpp")
#include "UI/Xaml/View/Window/MainWindow.g.cpp"
#endif

#include "Helpers/WindowHelper.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace Helpers::WinUIWindowHelper;

namespace winrt::VisualForge::UI::Xaml::View::Window::implementation
{
	MainWindow::MainWindow()
	{
		// AppWindow().SetIcon(L"Assets/AppIcons/win3264.ico");

		Closed([this](auto&&, auto&&)
			   {
				   PlacementRestoration::Save(*this);

				   // Stop ViewModel background thread (speed refresh)
				   try
				   {
					   winrt::VisualForge::ViewModels::MainViewModel vm = ViewModel();
					   if (vm)
					   {
						   vm.Shutdown();
					   }
				   }
				   catch (...)
				   {
					   OutputDebugStringA("MainWindow: ViewModel shutdown error\n");
				   }
			   });
	}

	VisualForge::ViewModels::MainViewModel MainWindow::ViewModel()
	{
		return MainContentView().ViewModel();
	}
}
