#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"

SecretTest::SecretTest() : State(SECRET_TEST),
    m_uiList(nullptr, &SecretTest::stringToStr, m_testList)
{

}

void SecretTest::onStateMounted(Device *PDN)
{
    m_testList.push_back("apple");
    m_testList.push_back("banana");
    m_testList.push_back("kiwi");
    m_testList.push_back("peach");
    m_testList.push_back("orange");
    m_testList.push_back("pear");
    m_testList.push_back("plum");
    m_testList.push_back("pineapple");
    m_testList.push_back("cherry");

    m_uiList.setDisplay(PDN->invalidateScreen());

    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::PRIMARY_BUTTON,
        SecretTest::pageDown, this);
    
    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::SECONDARY_BUTTON,
        SecretTest::pageUp, this);
}

void SecretTest::onStateLoop(Device *PDN)
{
    if(m_invalidated)
    {
        PDN->invalidateScreen();
        m_uiList.render();
        PDN->render();
        m_invalidated = false;
    }
}

void SecretTest::onStateDismounted(Device *PDN)
{
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
}

void SecretTest::stringToStr(const std::string *item, char *str, size_t str_max)
{
    strncpy(str, item->c_str(), str_max);
    str[str_max - 1] = '\0';
}

void SecretTest::pageUp(void* param)
{
    SecretTest* state = (SecretTest*)param;
    state->m_uiList.nextPage();
    state->m_invalidated = true;
}

void SecretTest::pageDown(void* param)
{
    SecretTest* state = (SecretTest*)param;
    state->m_uiList.prevPage();
    state->m_invalidated = true;
}