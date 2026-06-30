#pragma once

#include "UI/Xaml/View/Page/MainView.g.h"

namespace winrt::VisualForge::UI::Xaml::View::Page::implementation
{
    struct MainView : MainViewT<MainView>
    {
        MainView();
    };
}

namespace winrt::VisualForge::UI::Xaml::View::Page::factory_implementation
{
    struct MainView : MainViewT<MainView, implementation::MainView>
    {
    };
}
