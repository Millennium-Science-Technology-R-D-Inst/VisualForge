#pragma once

#include <cstddef>

namespace VisualForge::EditorCore::Viewport
{
    struct VisibleRange
    {
        std::size_t firstLine{ 0 };
        std::size_t lastLine{ 0 };
        std::size_t bufferedFirstLine{ 0 };
        std::size_t bufferedLastLine{ 0 };
    };
}
