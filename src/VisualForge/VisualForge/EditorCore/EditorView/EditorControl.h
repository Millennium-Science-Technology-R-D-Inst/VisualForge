#pragma once

#include "EditorCore/Document/TextDocument.h"
#include "EditorCore/Input/EditorInputController.h"
#include "EditorCore/Rendering/EditorRenderer.h"

#include <string_view>

namespace VisualForge::EditorCore::EditorView
{
    class EditorControl final
    {
    public:
        void Document(Document::TextDocument* document) noexcept;
        [[nodiscard]] Document::TextDocument* Document() const noexcept;
        [[nodiscard]] bool InsertText(std::wstring_view text);
        [[nodiscard]] bool Backspace();
        [[nodiscard]] bool DeleteForward();
        [[nodiscard]] bool MoveLeft();
        [[nodiscard]] bool MoveRight();
        void RenderVisibleContent();

    private:
        Document::TextDocument* m_document{ nullptr };
        Input::EditorInputController m_input;
        Rendering::EditorRenderer m_renderer;
    };
}
