#pragma once

#include <esp_log.h>

#include <device/pdn-display.hpp>

template <typename T>
class ListUI
{
public:
    typedef void (*ItemToStringHandler)(const T* item, char* str, size_t str_max_size);

    ListUI(Display* display, ItemToStringHandler callback, const std::vector<T>& items)  :
        m_display(display),
        m_items(items),
        m_itemToStringCallback(callback)
    {}

    void setDisplay(Display* display)
    {
        m_display = display;
    }

    void nextPage()
    {
        if(m_items.size() > (m_curPage + 1) * std::get<1>(m_display->getSizeInChar()))
            ++m_curPage;
    }

    void prevPage()
    {
        if(m_curPage > 0)
            --m_curPage;
    }

    void render()
    {
        int xSizeChar, ySizeChar;
        std::tie(xSizeChar, ySizeChar) = m_display->getSizeInChar();
        char* str = (char*)alloca(xSizeChar);
        size_t page_start = m_curPage * ySizeChar;
        int page_end = std::min(page_start + ySizeChar, m_items.size());

        if(page_start >= m_items.size())
        {
            ESP_LOGE("UI", "Attempt to render list past end: (listsize: %u) (page: %i)", m_items.size(), m_curPage);
            return;
        }
        //ESP_LOGD("UI", "items pointer: %p", m_items.data());

        for(int i = page_start; i < page_end; ++i)
        {
            const auto item = m_items.data() + i;
            m_itemToStringCallback(item, str, xSizeChar);
            //ESP_LOGD("UI", "Display::drawText(%s, 0, %d)", str, i-page_start+1);
            m_display->drawText(str, 0, i - page_start + 1);
        }
    }

protected:
    Display* m_display;
    const std::vector<T>& m_items;
    size_t m_curPage = 0;

    ItemToStringHandler m_itemToStringCallback;
};
