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
        if (value.empty())
        {
			return {};
        }

		auto const required = MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			value.data(),
			static_cast<int>(value.size()),
			nullptr,
			0);
		if (required <= 0)
		{
			return std::wstring{ value.begin(), value.end() };
		}

		std::wstring result(static_cast<std::size_t>(required), L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			value.data(),
			static_cast<int>(value.size()),
			result.data(),
			required);
		return result;
    }

    std::string JsonMessageSummaryParser::UnescapeJsonString(std::string_view value)
    {
        auto appendUtf8 = [](std::string& output, std::uint32_t codePoint)
        {
            if (codePoint <= 0x7F)
            {
                output.push_back(static_cast<char>(codePoint));
            }
            else if (codePoint <= 0x7FF)
            {
                output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else if (codePoint <= 0xFFFF)
            {
                output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else
            {
                output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        };

        std::string result;
        result.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            auto const current = value[index];
            if (current != '\\' || index + 1 >= value.size())
            {
				result.push_back(current);
				continue;
            }

			auto const escaped = value[++index];
			switch (escaped)
            {
			case '"': result.push_back('"'); break;
			case '\\': result.push_back('\\'); break;
			case '/': result.push_back('/'); break;
			case 'b': result.push_back('\b'); break;
			case 'f': result.push_back('\f'); break;
			case 'n': result.push_back('\n'); break;
			case 'r': result.push_back('\r'); break;
			case 't': result.push_back('\t'); break;
			case 'u':
				if (index + 4 < value.size())
				{
					std::uint32_t codePoint{};
					bool valid = true;
					for (std::size_t digit = 0; digit < 4; ++digit)
					{
						auto const hex = value[index + 1 + digit];
						codePoint <<= 4;
						if (hex >= '0' && hex <= '9') codePoint += static_cast<std::uint32_t>(hex - '0');
						else if (hex >= 'a' && hex <= 'f') codePoint += static_cast<std::uint32_t>(hex - 'a' + 10);
						else if (hex >= 'A' && hex <= 'F') codePoint += static_cast<std::uint32_t>(hex - 'A' + 10);
						else valid = false;
					}
					if (valid)
					{
						appendUtf8(result, codePoint);
						index += 4;
						break;
					}
				}
				result.push_back('u');
				break;
			default: result.push_back(escaped); break;
		    }
        }

        return result;
    }
}
