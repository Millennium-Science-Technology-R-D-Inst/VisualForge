#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace VisualForge::EditorCore::LSP
{
    struct JsonRpcMessage
    {
        std::string payload;
    };

    class JsonRpcFramer final
    {
    public:
        [[nodiscard]] static std::string Frame(std::string const& payload);
        [[nodiscard]] static std::size_t HeaderLength(std::string_view framedMessage);
        [[nodiscard]] static std::optional<std::size_t> ContentLength(std::string_view header);
    };

    class JsonRpcStreamParser final
    {
    public:
        [[nodiscard]] std::vector<JsonRpcMessage> Append(std::string_view bytes);
        [[nodiscard]] std::string const& BufferedBytes() const noexcept;
        void Clear();

    private:
        std::string m_buffer;
    };
}
