#pragma once
#include <ui/list-ui.hpp>

template <typename T>
class ListSelectUI : public ListUI<T>
{
public:
    typedef std::function<void(const T* item, char* str, size_t str_max_size)> ItemToStringHandler;

    ListSelectUI(Display* display, ItemToStringHandler callback, const std::vector<T>& items) :
        ListUI<T>(display, callback, items)
    {

    }

    const T* getSelectedItem()
    {
        return this->m_items.data() + m_curSelected;
    }

    void moveSelectionUp()
    {
        if(m_curSelected > 0)
        {
            m_curSelected--;
            recalculateCurrentPage();
        }
    }

    void moveSelectionDown()
    {
        if(m_curSelected < (this->m_items.size() - 1))
        {
            m_curSelected++;
            ESP_LOGD("UI", "Move Down, Selected: %d", m_curSelected);
            recalculateCurrentPage();
        }
    }

    void render() override
    {
        int xSizeChar, ySizeChar;
        std::tie(xSizeChar, ySizeChar) = this->m_display->getSizeInChar();
        char* str = (char*)alloca(xSizeChar);
        size_t page_start = this->m_curPage * ySizeChar;
        int page_end = std::min(page_start + ySizeChar, this->m_items.size());

        if(page_start >= this->m_items.size())
        {
            ESP_LOGE("UI", "Attempt to render list past end: (listsize: %u) (page: %i)", this->m_items.size(), this->m_curPage);
            return;
        }
        //ESP_LOGD("UI", "items pointer: %p", m_items.data());

        for(int i = page_start; i < page_end; ++i)
        {
            const auto item = this->m_items.data() + i;
            this->m_itemToStringCallback(item, str, xSizeChar);
            //ESP_LOGD("UI", "Display::drawText(%s, 0, %d)", str, i-page_start+1);
            if(i == m_curSelected)
                this->m_display->drawTextInvertedColor(str, 0, i - page_start + 1);
            else
                this->m_display->drawText(str, 0, i - page_start + 1);
        }
    }

protected:
    void recalculateCurrentPage()
    {
        int yDisplaySizeChars = std::get<1>(this->m_display->getSizeInChar());
        this->m_curPage = m_curSelected / yDisplaySizeChars;
    }

    size_t m_curSelected = 0;
};
