#pragma once

#include "App.xaml.g.h"

namespace winrt::VisualForge::implementation
{
    struct App : AppT<App>
    {
    public:
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

        static inline winrt::Microsoft::UI::Xaml::Window window{ nullptr };
    };
}
