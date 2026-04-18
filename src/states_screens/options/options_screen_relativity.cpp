//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2026 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifndef SERVER_ONLY

#include "states_screens/options/options_common.hpp"
#include "states_screens/options/options_screen_relativity.hpp"

#include "config/stk_config.hpp"

#include <sstream>

using namespace GUIEngine;

// -----------------------------------------------------------------------------

static const int SOL_MIN = 30;
static const int SOL_MAX = 1000;
static const int SOL_STEP = 10;

static void populateSOLSpinner(SpinnerWidget* w)
{
    w->clearLabels();
    for (int speed = SOL_MIN; speed <= SOL_MAX; speed += SOL_STEP)
    {
        std::ostringstream oss;
        oss << speed;
        w->addLabel(core::stringw(oss.str().c_str()));
    }
}

static int speedToIndex(int speed)
{
    speed = std::max(SOL_MIN, std::min(SOL_MAX, (speed / SOL_STEP) * SOL_STEP));
    return (speed - SOL_MIN) / SOL_STEP;
}

static int indexToSpeed(int index)
{
    return SOL_MIN + index * SOL_STEP;
}

// -----------------------------------------------------------------------------

OptionsScreenRelativity::OptionsScreenRelativity()
    : Screen("options/options_relativity.stkgui")
{
}   // OptionsScreenRelativity

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::loadedFromFile()
{
}   // loadedFromFile

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::init()
{
    Screen::init();
    OptionsCommon::setTabStatus();

    RibbonWidget* ribbon = getWidget<RibbonWidget>("options_choice");
    assert(ribbon != NULL);
    ribbon->setFocusForPlayer(PLAYER_ID_GAME_MASTER);
    ribbon->select("tab_relativity", PLAYER_ID_GAME_MASTER);

    SpinnerWidget* normal_w = getWidget<SpinnerWidget>("normal_speed_of_light");
    assert(normal_w != NULL);
    populateSOLSpinner(normal_w);
    normal_w->setValue(speedToIndex((int)UserConfigParams::m_relativity_speed_normal));

    SpinnerWidget* powerup_w = getWidget<SpinnerWidget>("powerup_speed_of_light");
    assert(powerup_w != NULL);
    populateSOLSpinner(powerup_w);
    powerup_w->setValue(speedToIndex((int)UserConfigParams::m_relativity_speed_powerup));

    // Apply the saved normal speed to the running config
    if (stk_config)
        stk_config->m_relativity_speed_of_light =
            (float)UserConfigParams::m_relativity_speed_normal;
}   // init

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::tearDown()
{
    Screen::tearDown();
    user_config->saveConfig();
}   // tearDown

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::eventCallback(Widget* widget,
                                             const std::string& name,
                                             const int playerID)
{
    if (name == "options_choice")
    {
        std::string selection =
            ((RibbonWidget*)widget)->getSelectionIDString(PLAYER_ID_GAME_MASTER);
        if (selection != "tab_relativity")
            OptionsCommon::switchTab(selection);
    }
    else if (name == "back")
    {
        StateManager::get()->escapePressed();
    }
    else if (name == "normal_speed_of_light")
    {
        SpinnerWidget* w = dynamic_cast<SpinnerWidget*>(widget);
        assert(w != NULL);
        int speed = indexToSpeed(w->getValue());
        UserConfigParams::m_relativity_speed_normal = speed;
        if (stk_config)
            stk_config->m_relativity_speed_of_light = (float)speed;
    }
    else if (name == "powerup_speed_of_light")
    {
        SpinnerWidget* w = dynamic_cast<SpinnerWidget*>(widget);
        assert(w != NULL);
        int speed = indexToSpeed(w->getValue());
        UserConfigParams::m_relativity_speed_powerup = speed;
    }
}   // eventCallback

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::unloaded()
{
}   // unloaded

// -----------------------------------------------------------------------------

#endif // ifndef SERVER_ONLY
