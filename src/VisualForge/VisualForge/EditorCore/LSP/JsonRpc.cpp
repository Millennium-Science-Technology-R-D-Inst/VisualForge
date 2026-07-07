#include "pch.h"

#include "EditorCore/LSP/JsonRpc.h"

#include <charconv>

namespace VisualForge::EditorCore::LSP
{
    std::string JsonRpcFramer::Frame(std::string const& payload)
    {
        return "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
    }

    std::size_t JsonRpcFramer::HeaderLength(std::string_view framedMessage)
    {
        auto const marker = framedMessage.find("\r\n\r\n");
        return marker == std::string_view::npos ? 0 : marker + 4;
    }

    std::optional<std::size_t> JsonRpcFramer::ContentLength(std::string_view header)
    {
        constexpr std::string_view key = "Content-Length:";
        auto const keyOffset = header.find(key);
        if (keyOffset == std::string_view::npos)
        {
            return std::nullopt;
        }

        auto valueStart = keyOffset + key.size();
        while (valueStart < header.size() && (header[valueStart] == ' ' || header[valueStart] == '\t'))
        {
            ++valueStart;
        }

        auto valueEnd = valueStart;
        while (valueEnd < header.size() && header[valueEnd] >= '0' && header[valueEnd] <= '9')
        {
            ++valueEnd;
        }

        std::size_t length{};
        auto const parsed = std::from_chars(header.data() + valueStart, header.data() + valueEnd, length);
        if (parsed.ec != std::errc{})
        {
            return std::nullopt;
        }

        return length;
    }

    std::vector<JsonRpcMessage> JsonRpcStreamParser::Append(std::string_view bytes)
    {
        m_buffer.append(bytes);
        std::vector<JsonRpcMessage> messages;

        for (;;)
        {
            auto const headerLength = JsonRpcFramer::HeaderLength(m_buffer);
            if (headerLength == 0)
            {
                break;
            }

            auto const contentLength = JsonRpcFramer::ContentLength(std::string_view{ m_buffer.data(), headerLength });
            if (!contentLength)
            {
                m_buffer.erase(0, headerLength);
                continue;
            }

            auto const totalLength = headerLength + *contentLength;
            if (m_buffer.size() < totalLength)
            {
                break;
            }

            messages.push_back({ m_buffer.substr(headerLength, *contentLength) });
            m_buffer.erase(0, totalLength);
        }

        return messages;
    }

    std::string const& JsonRpcStreamParser::BufferedBytes() const noexcept
    {
        return m_buffer;
    }

    void JsonRpcStreamParser::Clear()
    {
        m_buffer.clear();
    }
}
