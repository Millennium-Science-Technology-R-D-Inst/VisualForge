#pragma once

#include <cstddef>

namespace VisualForge::EditorCore::Layout
{
    struct TextLine
    {
        std::size_t startOffset{ 0 };
        std::size_t length{ 0 };
        float height{ 18.0f };
        float width{ 0.0f };
    };
}
