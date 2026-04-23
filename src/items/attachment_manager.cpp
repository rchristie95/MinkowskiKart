//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2006-2015 Joerg Henrichs
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

#include "items/attachment_manager.hpp"

#include "graphics/irr_driver.hpp"
#include "graphics/material.hpp"
#include "graphics/material_manager.hpp"
#include "graphics/sp/sp_base.hpp"
#include "guiengine/engine.hpp"
#include "guiengine/skin.hpp"
#include "io/file_manager.hpp"
#include "utils/log.hpp"

#include <IAnimatedMesh.h>

AttachmentManager *attachment_manager = 0;

struct  initAttachmentType {Attachment::AttachmentType attachment;
                            const char *file;
                            const char *icon_file;};

/* Some explanations to the attachments:
   Parachute: This will increase the air friction, reducing the maximum speed.
              It will not have too much of an effect on slow speeds, since air
              friction only becomes important at higher speeds.
   Anvil:     It increases the weight of the kart.But this will NOT have any
              effect on karts already driving at highest speed: the accelerating
       force is independent of the mass, so it is 0 at highest speed
       (engine force = air- plus system-force) and only this value gets
       divided by the mass later --> at highest speed there would be no
       effect when the mass is changed, only at lower speeds the acting
       acceleration will be lower.Reducing the power slows the kart down,
       but doesn't give the feeling of a sudden weight increase.
       Therefore the anvil will reduce by a certain factor (see physics
       parameters) once when it is attached. Together with the mass
       increase (lower acceleration) it's sufficient negative.
*/

static const initAttachmentType iat[]=
{
    {Attachment::ATTACH_TIME_DILATION,     "parachute.spm",        "parachute-attach-icon.png"    },
    {Attachment::ATTACH_BOMB,             "bomb.spm",             "bomb-attach-icon.png"         },
    {Attachment::ATTACH_MASS_SPIKE,       "harmonic-oscillator.spm", "harmonic-oscillator-icon.png" },
    {Attachment::ATTACH_SUPERPOSITION_CAT, "superposition-cat.spm", "super-position-icon.png"      },
    {Attachment::ATTACH_TIDAL_ARM,        "swatter.spm",          "swatter-icon.png"             },
    {Attachment::ATTACH_NOLOKS_SWATTER,   "swatter_nolok.spm",    "swatter-icon.png"             },
    {Attachment::ATTACH_TIDAL_ARM_ANIM,   "swatter_anim.spm",     "swatter-icon.png"             },
    {Attachment::ATTACH_WARP_BUBBLE,      "bubblegum_shield.spm", "shield-icon.png"              },
    {Attachment::ATTACH_NOLOK_WARP_BUBBLE, "bubblegum_shield_nolok.spm", "shield-icon.png"              },
    {Attachment::ATTACH_MAX,              "",                     ""                             },
};

//-----------------------------------------------------------------------------
AttachmentManager::AttachmentManager()
{
    for (int i = 0; i < Attachment::ATTACH_MAX; i++)
    {
        m_attachments[i] = NULL;
        m_all_icons[i] = NULL;
    }
}   // AttachmentManager

//-----------------------------------------------------------------------------
AttachmentManager::~AttachmentManager()
{
    for(int i=0; iat[i].attachment!=Attachment::ATTACH_MAX; i++)
    {
        scene::IMesh *mesh = m_attachments[iat[i].attachment];
        if (!mesh)
            continue;

        // Attachment meshes are owned through Irrlicht's mesh cache. Removing
        // them from the cache avoids dereferencing a stale attachment-held
        // pointer during shutdown/reload and still releases the cache's ref.
        if (irr_driver)
            irr_driver->removeMeshFromCache(mesh);
        m_attachments[iat[i].attachment] = NULL;
    }
}   // ~AttachmentManager

//-----------------------------------------------------------------------------
void AttachmentManager::loadModels()
{
    for(int i=0; iat[i].attachment!=Attachment::ATTACH_MAX; i++)
    {
        std::string full_path = file_manager->getAsset(FileManager::MODEL,iat[i].file);
        scene::IAnimatedMesh* mesh = irr_driver->getAnimatedMesh(full_path);
        if (!mesh)
        {
            Log::fatal("AttachmentManager", "Cannot load attachment mesh '%s'.",
                full_path.c_str());
            continue;
        }
#ifndef SERVER_ONLY
        SP::uploadSPM(mesh);
#endif
        m_attachments[iat[i].attachment] = mesh;
        if(iat[i].icon_file)
        {
            std::string full_icon_path     =
                GUIEngine::getSkin()->getThemedIcon(std::string("gui/icons/")
                                                    + iat[i].icon_file);
            m_all_icons[iat[i].attachment] =
                material_manager->getMaterial(full_icon_path,
                                              /*full_path*/             true,
                                              /*make_permanent*/        true,
                                              /*complain_if_not_found*/ true,
                                              /*strip_path*/            false);
        }
        if (GUIEngine::isNoGraphics())
            mesh->freeMeshVertexBuffer();
    }   // for
}   // reInit

