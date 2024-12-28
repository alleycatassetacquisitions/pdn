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
        char* str = (char*)alloca(this->m_strMaxLen);
        size_t page_start = this->m_curPage * this->m_pageLen;
        int page_end = std::min(page_start + this->m_pageLen, this->m_items.size());

        if(page_start >= this->m_items.size())
        {
            ESP_LOGE("UI", "Attempt to render list past end: (listsize: %u) (page: %i)", this->m_items.size(), this->m_curPage);
            return;
        }
        //ESP_LOGD("UI", "items pointer: %p", m_items.data());

        for(int i = page_start; i < page_end; ++i)
        {
            const auto item = this->m_items.data() + i;
            this->m_itemToStringCallback(item, str, this->m_strMaxLen);
            //ESP_LOGD("UI", "Display::drawText(%s, 0, %d)", str, i-page_start+1);
            int yDrawChar = i - page_start + 1 + this->m_yOffset;
            if(i == m_curSelected)
                this->m_display->drawTextInvertedColor(str, this->m_xOffset, yDrawChar);
            else
                this->m_display->drawText(str, this->m_xOffset, yDrawChar);
        }
    }

protected:
    void recalculateCurrentPage()
    {
        this->m_curPage = m_curSelected / this->m_pageLen;
    }

    size_t m_curSelected = 0;
};
