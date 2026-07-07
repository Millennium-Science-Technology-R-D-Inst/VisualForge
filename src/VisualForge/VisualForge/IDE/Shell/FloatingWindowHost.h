#pragma once

#include "IDE/Shell/DockingLayout.h"

#include <vector>
#include <winrt/Microsoft.UI.Xaml.h>

namespace VisualForge::IDE::Shell
{
    class FloatingWindowHost final
    {
    public:
        [[nodiscard]] bool Show(FloatingWindowDescriptor const& descriptor);
        void CloseAll() noexcept;
        [[nodiscard]] std::size_t Count() const noexcept;

    private:
        std::vector<winrt::Microsoft::UI::Xaml::Window> m_windows;
    };
}
