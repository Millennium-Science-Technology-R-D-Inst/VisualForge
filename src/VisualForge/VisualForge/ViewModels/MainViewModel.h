#pragma once

#include "ViewModels/MainViewModel.g.h"

namespace winrt::VisualForge::ViewModels::implementation
{
    struct MainViewModel : MainViewModelT<MainViewModel>
    {
        MainViewModel() = default;

    };
}

namespace winrt::VisualForge::ViewModels::factory_implementation
{
    struct MainViewModel : MainViewModelT<MainViewModel, implementation::MainViewModel>
    {
    };
}
