//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2016 C. Michael Murphey
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

#include "states_screens/help/help_common.hpp"
#include "online/link_helper.hpp"
#include "guiengine/widgets/list_widget.hpp"
#include "guiengine/CGUISpriteBank.hpp"
#include "graphics/irr_driver.hpp"

using namespace GUIEngine;

// -----------------------------------------------------------------------------

HelpScreen8::HelpScreen8() : Screen("help/help8.stkgui")
{
    m_icon_bank = NULL;
}   // HelpScreen8

// -----------------------------------------------------------------------------

void HelpScreen8::loadedFromFile()
{
    video::ITexture* icon0 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "minkowski.png"));
    video::ITexture* icon1 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "einstein.png"));
    video::ITexture* icon2 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "curie.png"));
    video::ITexture* icon3 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "feynman.png"));
    video::ITexture* icon4 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "maxwell.png"));
    video::ITexture* icon5 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "newton.png"));
    video::ITexture* icon6 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "noether.png"));
    video::ITexture* icon7 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "oppenheimer.png"));
    video::ITexture* icon8 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "planck.png"));
    video::ITexture* icon9 = irr_driver->getTexture(file_manager->getAsset(FileManager::GUI_ICON, "wikipedia.png"));

    m_icon_bank = new irr::gui::STKModifiedSpriteBank(GUIEngine::getGUIEnv());
    m_icon_bank->addTextureAsSprite(icon0);
    m_icon_bank->addTextureAsSprite(icon1);
    m_icon_bank->addTextureAsSprite(icon2);
    m_icon_bank->addTextureAsSprite(icon3);
    m_icon_bank->addTextureAsSprite(icon4);
    m_icon_bank->addTextureAsSprite(icon5);
    m_icon_bank->addTextureAsSprite(icon6);
    m_icon_bank->addTextureAsSprite(icon7);
    m_icon_bank->addTextureAsSprite(icon8);
    m_icon_bank->addTextureAsSprite(icon9);

    m_icon_bank->setScale(1.0f / 72.0f);
    m_icon_bank->setTargetIconSize(192, 192);
}   // loadedFromFile

// -----------------------------------------------------------------------------

void HelpScreen8::eventCallback(Widget* widget, const std::string& name, const int playerID)
{
    if (name == "category")
    {
        std::string selection = ((RibbonWidget*)widget)->getSelectionIDString(PLAYER_ID_GAME_MASTER);

        if (selection != "page8")
            HelpCommon::switchTab(selection);
    }
    else if (name == "back")
    {
        StateManager::get()->escapePressed();
    }
    else if (name == "racers_list")
    {
        ListWidget* w_list = getWidget<ListWidget>("racers_list");
        if (w_list != NULL)
        {
            std::string selection = w_list->getSelectionInternalName();
            if (selection == "minkowski")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/Hermann_Minkowski");
            else if (selection == "einstein")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/Albert_Einstein");
            else if (selection == "curie")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/Marie_Curie");
            else if (selection == "feynman")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/Richard_Feynman");
            else if (selection == "maxwell")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/James_Clerk_Maxwell");
            else if (selection == "newton")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/Isaac_Newton");
            else if (selection == "noether")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/Emmy_Noether");
            else if (selection == "oppenheimer")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/J._Robert_Oppenheimer");
            else if (selection == "planck")
                Online::LinkHelper::openURL("https://en.wikipedia.org/wiki/Max_Planck");
        }
    }
}   // eventCallback

// -----------------------------------------------------------------------------

void HelpScreen8::init()
{
    Screen::init();
    RibbonWidget* w = this->getWidget<RibbonWidget>("category");

    if (w != NULL)
    {
        w->setFocusForPlayer(PLAYER_ID_GAME_MASTER);
        w->select( "page8", PLAYER_ID_GAME_MASTER );
    }

    ListWidget* w_list = getWidget<ListWidget>("racers_list");
    assert(w_list != NULL);
    assert(m_icon_bank != NULL);

    w_list->clear();
    w_list->clearColumns();
    w_list->setIcons(m_icon_bank, 9.75f);

    w_list->addColumn(L"Portrait", 2);
    w_list->addColumn(L"Biography", 14);
    w_list->addColumn(L"Wikipedia", 2);

    auto addRacer = [&](const std::string& id, int portrait_idx, const std::string& name_bio) {
        std::vector<ListWidget::ListCell> row;
        row.push_back(ListWidget::ListCell("", portrait_idx, 2, true));
        row.push_back(ListWidget::ListCell(core::stringw(name_bio.c_str()), -1, 14, false));
        row.push_back(ListWidget::ListCell("", 9, 2, true));
        w_list->addItem(id, row);
    };

    addRacer("minkowski", 0, "Hermann Minkowski (1864--1909): Turned number theory into geometry through his geometry of numbers, showing how lattices and shapes could solve deep arithmetic problems. He then gave special relativity its natural geometric language: four-dimensional spacetime, where space and time are fused into one structure. His ideas became part of the mathematical stage on which modern relativity is written.");
    addRacer("einstein", 1, "Albert Einstein (1879--1955): Revealed that motion through space can affect motion through time: when you are still in space, your path points almost entirely through time, while near light speed your clock ticks far more slowly relative to the world you race past. In general relativity, he went further, showing that gravity is not a conventional force pulling objects together but the curvature of spacetime itself. He also helped launch quantum theory through his work on light quanta, Brownian motion, stimulated emission, and Bose--Einstein statistics.");
    addRacer("curie", 2, "Marie Curie (1867--1934): Revealed that atoms were not indivisible, unchanging objects, but could emit powerful radiation from within. She discovered polonium and radium, coined and developed the study of radioactivity, and became the first person to win two Nobel Prizes. Her work transformed atomic physics, chemistry, medicine, and cancer treatment.");
    addRacer("feynman", 3, "Richard Feynman (1918--1988): Reimagined quantum mechanics through the path integral formulation, where a quantum system is described by summing over all possible paths between two points, with those paths interfering to produce the observed outcome. He created Feynman diagrams, turning the strange behavior of subatomic particles into a visual and calculable language. His work shaped quantum electrodynamics, particle physics, quantum computing, and the modern intuition for quantum theory.");
    addRacer("maxwell", 4, "James Clerk Maxwell (1831--1879): Unified electricity, magnetism, and light into a single theory of electromagnetic fields. His equations showed that changing electric and magnetic fields can propagate through space as waves, and that light itself is an electromagnetic wave. In doing so, Maxwell built the bridge from Newtonian physics to modern field theory and set the stage for Einstein's relativity.");
    addRacer("newton", 5, "Isaac Newton (1643--1727): Built the first great mathematical system of physics, showing that the same laws govern falling bodies, ocean tides, cannonballs, and planets. His laws of motion and universal gravitation turned the universe into something that could be predicted with equations. He also independently developed calculus, revolutionized optics, and wrote the Principia, one of the most influential scientific works ever produced.");
    addRacer("noether", 6, "Emmy Noether (1882--1935): Discovered one of the deepest principles in physics: continuous symmetries of a physical system correspond to conservation laws. Time symmetry gives conservation of energy, space symmetry gives conservation of momentum, and rotational symmetry gives conservation of angular momentum. Her theorem became a foundation of modern theoretical physics, while her work in abstract algebra reshaped mathematics.");
    addRacer("oppenheimer", 7, "J. Robert Oppenheimer (1904--1967): Was a brilliant theoretical physicist who helped lead the Manhattan Project and became known as the father of the atomic bomb. Before that, he made important contributions to quantum mechanics, nuclear physics, neutron stars, and gravitational collapse, work connected to the modern theory of black holes. His life became a symbol of the power, danger, and moral weight of scientific discovery.");
    addRacer("planck", 8, "Max Planck (1858--1947): Proposed that energy is exchanged in tiny discrete packets called quanta, rather than in a perfectly continuous flow. This solution to the blackbody radiation problem opened the door to quantum mechanics. Planck's constant became the scale marker of the microscopic world, where nature stops behaving smoothly and starts behaving quantum mechanically.");
}   // init

// -----------------------------------------------------------------------------

void HelpScreen8::unloaded()
{
    delete m_icon_bank;
    m_icon_bank = NULL;
}   // unloaded

// -----------------------------------------------------------------------------
