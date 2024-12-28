#pragma once

#include <esp_log.h>

#include <device/pdn-display.hpp>

template <typename T>
class ListUI
{
public:
    typedef std::function<void(const T* item, char* str, size_t str_max_size)> ItemToStringHandler;

    ListUI(Display* display, ItemToStringHandler callback, const std::vector<T>& items)  :
        m_display(display),
        m_items(items),
        m_itemToStringCallback(callback)
    {
        updateLengths();
    }

    void setDisplay(Display* display)
    {
        m_display = display;
        updateLengths();
    }

    void setPos(int xOffsetChars, int yOffsetChars)
    {
        m_xOffset = xOffsetChars;
        m_yOffset = yOffsetChars;
        updateLengths();
    }

    void nextPage()
    {
        if(m_items.size() > (m_curPage + 1) * m_pageLen)
            ++m_curPage;
    }

    void prevPage()
    {
        if(m_curPage > 0)
            --m_curPage;
    }

    virtual void render()
    {
        if(!m_display)
        {
            ESP_LOGW("UI", "Attempt to render ListUI without attached display");
            return;
        }

        char* str = (char*)alloca(m_strMaxLen);
        size_t page_start = m_curPage * m_pageLen;
        int page_end = std::min(page_start + m_pageLen, m_items.size());

        if(page_start >= m_items.size())
        {
            ESP_LOGE("UI", "Attempt to render list past end: (listsize: %u) (page: %i)", m_items.size(), m_curPage);
            return;
        }
        //ESP_LOGD("UI", "items pointer: %p", m_items.data());

        for(int i = page_start; i < page_end; ++i)
        {
            const auto item = m_items.data() + i;
            m_itemToStringCallback(item, str, m_strMaxLen);
            //ESP_LOGD("UI", "Display::drawText(%s, 0, %d)", str, i-page_start+1);
            m_display->drawText(str, m_xOffset, i - page_start + 1 + m_yOffset);
        }
    }

protected:
    Display* m_display;
    const std::vector<T>& m_items;
    size_t m_curPage = 0;

    int m_xOffset = 0;
    int m_yOffset = 0;
    int m_strMaxLen;
    int m_pageLen;

    ItemToStringHandler m_itemToStringCallback;

    void updateLengths()
    {
        if(!m_display)
            return;

        int xSizeChar, ySizeChar;
        std::tie(xSizeChar, ySizeChar) = m_display->getSizeInChar();

        m_strMaxLen = xSizeChar - m_xOffset;
        m_pageLen = ySizeChar - m_yOffset;
    }
};
