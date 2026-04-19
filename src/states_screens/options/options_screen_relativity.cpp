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
#include "relativity/relativity_math.hpp"

#include <sstream>

using namespace GUIEngine;

// -----------------------------------------------------------------------------

static const int C_LIGHT_MIN = 30;
static const int C_LIGHT_MAX = 1000;
static const int C_LIGHT_STEP = 10;

static void populateCLightSpinner(SpinnerWidget* w)
{
    w->clearLabels();
    for (int c_light = C_LIGHT_MIN; c_light <= C_LIGHT_MAX;
         c_light += C_LIGHT_STEP)
    {
        std::ostringstream oss;
        oss << c_light;
        w->addLabel(core::stringw(oss.str().c_str()));
    }
}

static int cLightToIndex(int c_light)
{
    c_light = std::max(C_LIGHT_MIN,
        std::min(C_LIGHT_MAX, (c_light / C_LIGHT_STEP) * C_LIGHT_STEP));
    return (c_light - C_LIGHT_MIN) / C_LIGHT_STEP;
}

static int indexToCLight(int index)
{
    return C_LIGHT_MIN + index * C_LIGHT_STEP;
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

    SpinnerWidget* normal_w = getWidget<SpinnerWidget>("normal_c_light");
    assert(normal_w != NULL);
    populateCLightSpinner(normal_w);
    normal_w->setValue(cLightToIndex(
        (int)UserConfigParams::m_relativity_normal_c_light));

    SpinnerWidget* powerup_w = getWidget<SpinnerWidget>("powerup_c_light");
    assert(powerup_w != NULL);
    populateCLightSpinner(powerup_w);
    powerup_w->setValue(cLightToIndex(
        (int)UserConfigParams::m_relativity_powerup_c_light));

    Relativity::getCurrentCLight();
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
    else if (name == "normal_c_light")
    {
        SpinnerWidget* w = dynamic_cast<SpinnerWidget*>(widget);
        assert(w != NULL);
        const int c_light = indexToCLight(w->getValue());
        UserConfigParams::m_relativity_normal_c_light = c_light;
        Relativity::getCurrentCLight();
    }
    else if (name == "powerup_c_light")
    {
        SpinnerWidget* w = dynamic_cast<SpinnerWidget*>(widget);
        assert(w != NULL);
        const int c_light = indexToCLight(w->getValue());
        UserConfigParams::m_relativity_powerup_c_light = c_light;
        Relativity::getCurrentCLight();
    }
}   // eventCallback

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::unloaded()
{
}   // unloaded

// -----------------------------------------------------------------------------

#endif // ifndef SERVER_ONLY
