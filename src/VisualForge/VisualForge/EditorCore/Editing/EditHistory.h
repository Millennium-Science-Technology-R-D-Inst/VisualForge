#pragma once

#include "EditorCore/Editing/EditCommand.h"
#include "EditorCore/TextModel/TextBuffer.h"

#include <vector>

namespace VisualForge::EditorCore::Editing
{
    class EditHistory final
    {
    public:
        void Execute(TextModel::TextBuffer& buffer, EditCommand command);
        bool Undo(TextModel::TextBuffer& buffer);
        bool Redo(TextModel::TextBuffer& buffer);
        void Clear();

        [[nodiscard]] bool CanUndo() const noexcept;
        [[nodiscard]] bool CanRedo() const noexcept;

    private:
        static EditCommand Inverse(EditCommand const& command);
        void Apply(TextModel::TextBuffer& buffer, EditCommand const& command);

        std::vector<EditCommand> m_undo;
        std::vector<EditCommand> m_redo;
    };
}
