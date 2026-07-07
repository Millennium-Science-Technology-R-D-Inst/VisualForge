#include "pch.h"

#include "EditorCore/Rendering/DirectWriteContext.h"

#include <atomic>

namespace VisualForge::EditorCore::Rendering
{
    namespace
    {
        class MetricsTextRenderer final : public IDWriteTextRenderer
        {
        public:
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
            {
                if (!object)
                {
                    return E_POINTER;
                }

                if (riid == __uuidof(IUnknown)
                    || riid == __uuidof(IDWritePixelSnapping)
                    || riid == __uuidof(IDWriteTextRenderer))
                {
                    *object = static_cast<IDWriteTextRenderer*>(this);
                    AddRef();
                    return S_OK;
                }

                *object = nullptr;
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return ++m_refCount;
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                auto const count = --m_refCount;
                if (count == 0)
                {
                    delete this;
                }

                return count;
            }

            HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override
            {
                if (!isDisabled)
                {
                    return E_POINTER;
                }

                *isDisabled = FALSE;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override
            {
                if (!transform)
                {
                    return E_POINTER;
                }

                *transform = DWRITE_MATRIX{ 1, 0, 0, 1, 0, 0 };
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override
            {
                if (!pixelsPerDip)
                {
                    return E_POINTER;
                }

                *pixelsPerDip = 1.0f;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DrawGlyphRun(
                void*,
                FLOAT,
                FLOAT,
                DWRITE_MEASURING_MODE,
                DWRITE_GLYPH_RUN const* glyphRun,
                DWRITE_GLYPH_RUN_DESCRIPTION const*,
                IUnknown*) override
            {
                ++Metrics.GlyphRunCount;
                if (glyphRun)
                {
                    Metrics.GlyphCount += glyphRun->glyphCount;
                }

                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override
            {
                return S_OK;
            }

            DirectWriteRenderMetrics Metrics;

        private:
            std::atomic<ULONG> m_refCount{ 1 };
        };
    }

    bool DirectWriteContext::EnsureInitialized()
    {
        if (m_isInitialized)
        {
            return true;
        }

        winrt::com_ptr<IUnknown> factoryUnknown;
        if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            factoryUnknown.put())))
        {
            return false;
        }

        m_factory = factoryUnknown.as<IDWriteFactory>();
        if (FAILED(m_factory->CreateTextFormat(
            L"Cascadia Mono",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            13.0f,
            L"",
            m_textFormat.put())))
        {
            m_factory = nullptr;
            return false;
        }

        m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_isInitialized = true;
        return true;
    }

    bool DirectWriteContext::IsInitialized() const noexcept
    {
        return m_isInitialized;
    }

    winrt::com_ptr<IDWriteTextLayout> DirectWriteContext::CreateTextLayout(
        std::wstring_view text,
        float maxWidth,
        float maxHeight)
    {
        winrt::com_ptr<IDWriteTextLayout> layout;
        if (!EnsureInitialized())
        {
            return layout;
        }

        auto const width = maxWidth > 1.0f ? maxWidth : 1.0f;
        auto const height = maxHeight > 1.0f ? maxHeight : 1.0f;
        if (FAILED(m_factory->CreateTextLayout(
            text.data(),
            static_cast<UINT32>(text.size()),
            m_textFormat.get(),
            width,
            height,
            layout.put())))
        {
            return {};
        }

        return layout;
    }

    DirectWriteRenderMetrics DirectWriteContext::DrawTextLayout(IDWriteTextLayout* layout)
    {
        DirectWriteRenderMetrics metrics;
        if (!layout)
        {
            return metrics;
        }

        winrt::com_ptr<MetricsTextRenderer> renderer;
        renderer.attach(new MetricsTextRenderer{});
        auto const hr = layout->Draw(nullptr, renderer.get(), 0.0f, 0.0f);
        metrics = renderer->Metrics;
        metrics.Succeeded = SUCCEEDED(hr);
        return metrics;
    }
}
