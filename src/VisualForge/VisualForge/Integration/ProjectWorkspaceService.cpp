#include "pch.h"

#include "Integration/ProjectWorkspaceService.h"

#include <fstream>
#include <winrt/Windows.Data.Xml.Dom.h>

using namespace winrt;

namespace VisualForge::Integration
{
    namespace
    {
        constexpr wchar_t const* ProjectNamespace = L"http://schemas.microsoft.com/developer/msbuild/2003";

        winrt::Windows::Foundation::IAsyncOperation<Windows::Data::Xml::Dom::XmlDocument> LoadProject(std::filesystem::path const& projectPath)
        {
            // LoadFromUriAsync accepts a URL, not a raw Windows path. Convert the
            // local path to a file URI so the XML loader reads the file itself,
            // including its declared encoding and UTF-8 BOM.
            auto const uri = Windows::Foundation::Uri{
                hstring{ L"file:///" + projectPath.generic_wstring() } };
            co_return co_await Windows::Data::Xml::Dom::XmlDocument::LoadFromUriAsync(uri);
        }

        bool SaveProject(Windows::Data::Xml::Dom::XmlDocument const& document,
                         std::filesystem::path const& projectPath, std::wstring& error)
        {
            std::ofstream stream{ projectPath, std::ios::binary | std::ios::trunc };
            if (!stream)
            {
                error = L"Project file could not be written.";
                return false;
            }

            auto const xml = to_string(document.GetXml());
            stream.write(xml.data(), static_cast<std::streamsize>(xml.size()));
            if (!stream)
            {
                error = L"Project file write failed.";
                return false;
            }
            return true;
        }

        std::wstring RelativeInclude(std::filesystem::path const& projectPath,
                                     std::filesystem::path const& itemPath)
        {
            return itemPath.lexically_relative(projectPath.parent_path()).generic_wstring();
        }

        Windows::Data::Xml::Dom::XmlNodeList ItemNodes(
            Windows::Data::Xml::Dom::XmlDocument const& document)
        {
            return document.SelectNodes(
                L"//*[local-name()='ClCompile' or local-name()='ClInclude' or local-name()='Page' "
                L"or local-name()='None' or local-name()='ResourceCompile']");
        }

        Windows::Data::Xml::Dom::XmlElement FindItemGroup(
            Windows::Data::Xml::Dom::XmlDocument const& document)
        {
            auto groups = document.SelectNodes(L"//*[local-name()='ItemGroup']");
            for (uint32_t index = 0; index < groups.Length(); ++index)
            {
                auto group = groups.Item(index).as<Windows::Data::Xml::Dom::XmlElement>();
                if (!group.Attributes().GetNamedItem(L"Label"))
                {
                    return group;
                }
            }
            return nullptr;
        }

        bool FindMatchingItem(Windows::Data::Xml::Dom::XmlNodeList const& nodes,
                              std::wstring const& include, Windows::Data::Xml::Dom::IXmlNode& result)
        {
            for (uint32_t index = 0; index < nodes.Length(); ++index)
            {
                auto node = nodes.Item(index);
                auto attribute = node.Attributes().GetNamedItem(L"Include");
                if (attribute && attribute.NodeValue()
                    && std::wstring{ unbox_value<hstring>(attribute.NodeValue()) } == include)
                {
                    result = node;
                    return true;
                }
            }
            return false;
        }
    }

    winrt::Windows::Foundation::IAsyncAction ProjectWorkspaceService::ReadConfigurations(
        std::filesystem::path const& projectPath,
        std::vector<ProjectConfigurationEntry>& result)
    {
        auto document = co_await LoadProject(projectPath);
        auto nodes = document.SelectNodes(L"//*[local-name()='ProjectConfiguration']");
        for (uint32_t index = 0; index < nodes.Length(); ++index)
        {
            auto include = nodes.Item(index).Attributes().GetNamedItem(L"Include");
            if (!include || !include.NodeValue())
            {
                continue;
            }
            auto value = std::wstring{ unbox_value<hstring>(include.NodeValue()) };
            auto separator = value.find(L'|');
            if (separator != std::wstring::npos)
            {
                result.push_back({ value.substr(0, separator), value.substr(separator + 1) });
            }
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> ProjectWorkspaceService::ContainsItem(
        std::filesystem::path const& projectPath,
        std::filesystem::path const& itemPath)
    {
        try
        {
            auto document = co_await LoadProject(projectPath);
            auto nodes = ItemNodes(document);
            Windows::Data::Xml::Dom::IXmlNode item{ nullptr };
            co_return FindMatchingItem(nodes, RelativeInclude(projectPath, itemPath), item);
        }
        catch (...)
        {
            co_return false;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> ProjectWorkspaceService::AddItem(std::filesystem::path const& projectPath,
                                          std::filesystem::path const& itemPath, std::wstring itemKind, std::wstring& error)
    {
        try
        {
            auto document = co_await LoadProject(projectPath);
            auto const include = RelativeInclude(projectPath, itemPath);
            auto nodes = ItemNodes(document);
            Windows::Data::Xml::Dom::IXmlNode existing{ nullptr };
            if (FindMatchingItem(nodes, include, existing))
            {
                error = L"The project already contains this item.";
                co_return false;
            }

            auto group = FindItemGroup(document);
            if (!group)
            {
                group = document.CreateElementNS(box_value(hstring{ ProjectNamespace }), L"ItemGroup");
                document.DocumentElement().AppendChild(group);
            }
            auto item = document.CreateElementNS(box_value(hstring{ ProjectNamespace }), hstring{ itemKind });
            item.SetAttribute(L"Include", hstring{ include });
            group.AppendChild(item);
            co_return SaveProject(document, projectPath, error);
        }
        catch (hresult_error const& exception)
        {
            error = std::wstring{ L"Project XML error: " } + std::wstring{ exception.message() };
            co_return false;
        }
        catch (std::exception const& exception)
        {
            error = std::wstring{ L"Project error: " } + to_hstring(exception.what()).c_str();
            co_return false;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> ProjectWorkspaceService::RenameItem(std::filesystem::path const& projectPath,
                                             std::filesystem::path const& oldPath, std::filesystem::path const& newPath, std::wstring& error)
    {
        try
        {
            auto document = co_await LoadProject(projectPath);
            auto nodes = ItemNodes(document);
            Windows::Data::Xml::Dom::IXmlNode item{ nullptr };
            if (!FindMatchingItem(nodes, RelativeInclude(projectPath, oldPath), item))
            {
                co_return true;
            }
            item.Attributes().GetNamedItem(L"Include").NodeValue(
                box_value(hstring{ RelativeInclude(projectPath, newPath) }));
            co_return SaveProject(document, projectPath, error);
        }
        catch (hresult_error const& exception)
        {
            error = std::wstring{ L"Project XML error: " } + std::wstring{ exception.message() };
            co_return false;
        }
        catch (std::exception const& exception)
        {
            error = std::wstring{ L"Project error: " } + to_hstring(exception.what()).c_str();
            co_return false;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> ProjectWorkspaceService::RemoveItem(std::filesystem::path const& projectPath,
                                             std::filesystem::path const& itemPath, std::wstring& error)
    {
        try
        {
            auto document = co_await LoadProject(projectPath);
            auto nodes = ItemNodes(document);
            Windows::Data::Xml::Dom::IXmlNode item{ nullptr };
            if (!FindMatchingItem(nodes, RelativeInclude(projectPath, itemPath), item))
            {
                co_return true;
            }
            item.ParentNode().RemoveChild(item);
            co_return SaveProject(document, projectPath, error);
        }
        catch (hresult_error const& exception)
        {
            error = std::wstring{ L"Project XML error: " } + std::wstring{ exception.message() };
            co_return false;
        }
        catch (std::exception const& exception)
        {
            error = std::wstring{ L"Project error: " } + to_hstring(exception.what()).c_str();
            co_return false;
        }
    }
}
