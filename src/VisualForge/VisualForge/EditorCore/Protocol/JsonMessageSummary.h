#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace VisualForge::EditorCore::Protocol
{
    struct JsonMessageSummary
    {
        std::optional<int> Id;
        std::wstring Method;
        std::wstring Event;
        std::wstring Command;
        std::optional<bool> Success;
    };

    class JsonMessageSummaryParser final
    {
    public:
        [[nodiscard]] static JsonMessageSummary Parse(std::string_view payload);
        [[nodiscard]] static std::optional<std::wstring> ReadStringField(std::string_view payload, std::string_view fieldName);
        [[nodiscard]] static std::optional<int> ReadIntField(std::string_view payload, std::string_view fieldName);
        [[nodiscard]] static std::optional<bool> ReadBoolField(std::string_view payload, std::string_view fieldName);

    private:
        [[nodiscard]] static std::optional<std::string_view> ReadRawFieldValue(std::string_view payload, std::string_view fieldName);
        [[nodiscard]] static std::wstring WidenUtf8Lossy(std::string_view value);
        [[nodiscard]] static std::string UnescapeJsonString(std::string_view value);
    };
}
