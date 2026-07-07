#pragma once

#include <cstddef>
#include <string>

namespace VisualForge::EditorCore::Editing
{
    struct EditCommand
    {
        enum class Type
        {
            Insert,
            Delete
        };

        Type type{ Type::Insert };
        std::size_t position{ 0 };
        std::wstring text;
    };
}
