//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2026 MinkowskiKart-Team
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
#include "graphics/irr_driver.hpp"
#include "relativity/relativity_math.hpp"
#include "utils/log.hpp"

#include <cmath>
#include <sstream>

using namespace GUIEngine;

// -----------------------------------------------------------------------------

static const int C_LIGHT_MIN = 15;
static const int C_LIGHT_MAX = 1000;
static const int C_LIGHT_STEP = 5;

// max_beta stored as an integer percentage (10 = 0.10 .. 99 = 0.99)
static const int MAX_BETA_MIN_PCT  = 10;
static const int MAX_BETA_MAX_PCT  = 99;

static void populateMaxBetaSpinner(SpinnerWidget* w)
{
    w->clearLabels();
    for (int pct = MAX_BETA_MIN_PCT; pct <= MAX_BETA_MAX_PCT; pct++)
    {
        std::ostringstream oss;
        oss << "0." << (pct < 10 ? "0" : "") << pct << "c";
        w->addLabel(core::stringw(oss.str().c_str()));
    }
}

static int maxBetaToIndex(float beta)
{
    int pct = (int)std::round((double)beta * 100.0);
    pct = std::max(MAX_BETA_MIN_PCT, std::min(MAX_BETA_MAX_PCT, pct));
    return pct - MAX_BETA_MIN_PCT;
}

static float indexToMaxBeta(int index)
{
    return (float)(MAX_BETA_MIN_PCT + index) / 100.0f;
}

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

static int trackClippingModeToIndex(
    UserConfigParams::RelativityTrackClippingMode mode)
{
    return mode == UserConfigParams::RelativityTrackClippingMode::
        ENHANCED_DYNAMIC_SUBDIVISION ? 1 : 0;
}

// -----------------------------------------------------------------------------

OptionsScreenRelativity::OptionsScreenRelativity()
    : Screen("options/options_relativity.stkgui"),
      m_previous_track_clipping_mode(0)
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
    if (ribbon == NULL)
    {
        Log::error("OptionsScreenRelativity",
            "Missing options_choice widget in options_relativity.stkgui.");
        return;
    }
    ribbon->setFocusForPlayer(PLAYER_ID_GAME_MASTER);
    ribbon->select("tab_relativity", PLAYER_ID_GAME_MASTER);

    SpinnerWidget* normal_w = getWidget<SpinnerWidget>("normal_c_light");
    if (normal_w == NULL)
    {
        Log::error("OptionsScreenRelativity",
            "Missing normal_c_light widget in options_relativity.stkgui.");
        return;
    }
    populateCLightSpinner(normal_w);
    normal_w->setValue(cLightToIndex(
        (int)UserConfigParams::m_relativity_normal_c_light));

    SpinnerWidget* beta_w = getWidget<SpinnerWidget>("max_beta");
    if (beta_w == NULL)
    {
        Log::error("OptionsScreenRelativity",
            "Missing max_beta widget in options_relativity.stkgui.");
        return;
    }
    populateMaxBetaSpinner(beta_w);
    beta_w->setValue(maxBetaToIndex(
        (float)UserConfigParams::m_relativity_max_beta));

    SpinnerWidget* clipping_w =
        getWidget<SpinnerWidget>("track_clipping_mode");
    if (clipping_w == NULL)
    {
        Log::error("OptionsScreenRelativity",
            "Missing track_clipping_mode widget in options_relativity.stkgui.");
        return;
    }
    clipping_w->clearLabels();
    clipping_w->addLabel(core::stringw(
        _("Cheap (lite subdivision + height correction)")));
    clipping_w->addLabel(core::stringw(
        _("Enhanced (strong subdivision)")));
    m_previous_track_clipping_mode = trackClippingModeToIndex(
        Relativity::getConfiguredTrackClippingMode());
    clipping_w->setValue(m_previous_track_clipping_mode);

    const bool in_game =
        StateManager::get()->getGameState() == GUIEngine::INGAME_MENU;
    if (!Relativity::supportsEnhancedTrackClipping())
    {
        clipping_w->setActive(false);
        clipping_w->setTooltip(_("Track subdivision requires OpenGL 4.0 "
            "or OpenGL ES 3.2. Cheap height correction will be used "
            "without GPU subdivision."));
    }
    else
    {
        clipping_w->setActive(!in_game);
        OptionsCommon::updatePauseTooltip(clipping_w, in_game);
    }
    updateTrackClippingDescription();

    Relativity::getCurrentCLight();
}   // init

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::updateTrackClippingDescription()
{
    LabelWidget* description =
        getWidget<LabelWidget>("track_clipping_description");
    if (description == NULL)
        return;

    if (Relativity::getConfiguredTrackClippingMode() ==
        UserConfigParams::RelativityTrackClippingMode::
            ENHANCED_DYNAMIC_SUBDIVISION)
    {
        if (Relativity::supportsEnhancedTrackClipping())
        {
            description->setText(_("Dynamically subdivides warped meshes for "
                "a more realistic surface using a stronger near-kart cutoff. "
                "Higher GPU cost; height correction is disabled."), false);
        }
        else
        {
            description->setText(_("Enhanced is selected but unavailable on "
                "this device. Cheap height correction will be used without "
                "GPU subdivision."), false);
        }
    }
    else
    {
        description->setText(_("Uses lite dynamic subdivision when available "
            "and adjusts kart height to reduce track clipping."), false);
    }
}   // updateTrackClippingDescription

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::tearDown()
{
    const int current_track_clipping_mode = trackClippingModeToIndex(
        Relativity::getConfiguredTrackClippingMode());
    if (m_previous_track_clipping_mode != current_track_clipping_mode &&
        Relativity::supportsEnhancedTrackClipping())
    {
        irr_driver->sameRestart();
    }
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
        Relativity::setCurrentCLight((float)c_light);
    }
    else if (name == "max_beta")
    {
        SpinnerWidget* w = dynamic_cast<SpinnerWidget*>(widget);
        assert(w != NULL);
        UserConfigParams::m_relativity_max_beta = indexToMaxBeta(w->getValue());
    }
    else if (name == "track_clipping_mode")
    {
        SpinnerWidget* w = dynamic_cast<SpinnerWidget*>(widget);
        assert(w != NULL);
        UserConfigParams::m_relativity_track_clipping_mode =
            w->getValue() == 1
                ? (int)UserConfigParams::RelativityTrackClippingMode::
                    ENHANCED_DYNAMIC_SUBDIVISION
                : (int)UserConfigParams::RelativityTrackClippingMode::
                    CHEAP_HEIGHT_CORRECTION;
        updateTrackClippingDescription();
    }
}   // eventCallback

// -----------------------------------------------------------------------------

void OptionsScreenRelativity::unloaded()
{
}   // unloaded

// -----------------------------------------------------------------------------

#endif // ifndef SERVER_ONLY
