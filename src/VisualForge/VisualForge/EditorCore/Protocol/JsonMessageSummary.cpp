#include "pch.h"

#include "EditorCore/Protocol/JsonMessageSummary.h"

#include <charconv>

namespace VisualForge::EditorCore::Protocol
{
    JsonMessageSummary JsonMessageSummaryParser::Parse(std::string_view payload)
    {
        return {
            ReadIntField(payload, "id"),
            ReadStringField(payload, "method").value_or(L""),
            ReadStringField(payload, "event").value_or(L""),
            ReadStringField(payload, "command").value_or(L""),
            ReadBoolField(payload, "success")
        };
    }

    std::optional<std::wstring> JsonMessageSummaryParser::ReadStringField(std::string_view payload, std::string_view fieldName)
    {
        auto value = ReadRawFieldValue(payload, fieldName);
        if (!value || value->size() < 2 || value->front() != '"' || value->back() != '"')
        {
            return std::nullopt;
        }

        return WidenUtf8Lossy(UnescapeJsonString(value->substr(1, value->size() - 2)));
    }

    std::optional<int> JsonMessageSummaryParser::ReadIntField(std::string_view payload, std::string_view fieldName)
    {
        auto value = ReadRawFieldValue(payload, fieldName);
        if (!value)
        {
            return std::nullopt;
        }

        int result{};
        auto const parsed = std::from_chars(value->data(), value->data() + value->size(), result);
        if (parsed.ec != std::errc{})
        {
            return std::nullopt;
        }

        return result;
    }

    std::optional<bool> JsonMessageSummaryParser::ReadBoolField(std::string_view payload, std::string_view fieldName)
    {
        auto value = ReadRawFieldValue(payload, fieldName);
        if (!value)
        {
            return std::nullopt;
        }

        if (*value == "true")
        {
            return true;
        }

        if (*value == "false")
        {
            return false;
        }

        return std::nullopt;
    }

    std::optional<std::string_view> JsonMessageSummaryParser::ReadRawFieldValue(std::string_view payload, std::string_view fieldName)
    {
        std::string needle;
        needle.reserve(fieldName.size() + 4);
        needle.push_back('"');
        needle.append(fieldName);
        needle.push_back('"');

        auto const field = payload.find(needle);
        if (field == std::string_view::npos)
        {
            return std::nullopt;
        }

        auto colon = payload.find(':', field + needle.size());
        if (colon == std::string_view::npos)
        {
            return std::nullopt;
        }

        auto valueStart = colon + 1;
        while (valueStart < payload.size() && (payload[valueStart] == ' ' || payload[valueStart] == '\t' || payload[valueStart] == '\r' || payload[valueStart] == '\n'))
        {
            ++valueStart;
        }

        if (valueStart >= payload.size())
        {
            return std::nullopt;
        }

        if (payload[valueStart] == '"')
        {
            auto escaped = false;
            for (auto index = valueStart + 1; index < payload.size(); ++index)
            {
                auto const current = payload[index];
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if (current == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (current == '"')
                {
                    return payload.substr(valueStart, index - valueStart + 1);
                }
            }

            return std::nullopt;
        }

        auto valueEnd = valueStart;
        while (valueEnd < payload.size()
            && payload[valueEnd] != ','
            && payload[valueEnd] != '}'
            && payload[valueEnd] != ']'
            && payload[valueEnd] != '\r'
            && payload[valueEnd] != '\n')
        {
            ++valueEnd;
        }

        while (valueEnd > valueStart && payload[valueEnd - 1] == ' ')
        {
            --valueEnd;
        }

        return payload.substr(valueStart, valueEnd - valueStart);
    }

    std::wstring JsonMessageSummaryParser::WidenUtf8Lossy(std::string_view value)
    {
        std::wstring result;
        result.reserve(value.size());
        for (auto const current : value)
        {
            result.push_back(static_cast<unsigned char>(current) < 0x80
                ? static_cast<wchar_t>(current)
                : L'?');
        }

        return result;
    }

    std::string JsonMessageSummaryParser::UnescapeJsonString(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        auto escaped = false;
        for (auto const current : value)
        {
            if (escaped)
            {
                switch (current)
                {
                case '"':
                    result.push_back('"');
                    break;
                case '\\':
                    result.push_back('\\');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                default:
                    result.push_back(current);
                    break;
                }

                escaped = false;
            }
            else if (current == '\\')
            {
                escaped = true;
            }
            else
            {
                result.push_back(current);
            }
        }

        return result;
    }
}
