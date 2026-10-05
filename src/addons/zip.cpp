//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2010-2015 Lucas Baudin
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

#include <string.h>
#include <iostream>
#include <fstream>
#include <stdint.h>

#include "addons/zip.hpp"
#include "graphics/irr_driver.hpp"
#include "io/file_manager.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

#include <IrrlichtDevice.h>
#include <IFileSystem.h>
#include <IReadFile.h>
#include <IWriteFile.h>
using namespace irr;
using namespace io;

bool copyFileBounded(IWriteFile* dst, IReadFile* src, uint64_t expected_size)
{
  char buf[8192];
  uint64_t remaining = expected_size;
  while (remaining > 0)
  {
    const u32 requested = (u32)(remaining < sizeof(buf) ? remaining : sizeof(buf));
    const s32 read = src->read(buf, requested);
    if (read <= 0 || (u32)read > requested) return false;
    s32 written = 0;
    while (written < read)
    {
      const s32 n = dst->write(buf + written, read - written);
      if (n <= 0) return false;
      written += n;
    }
    remaining -= (u32)read;
  }
  return src->getPos() == src->getSize();
}

// ----------------------------------------------------------------------------
/** Extracts all files from the zip archive 'from' to the directory 'to'.
 *  \param from A zip archive.
 *  \param to The destination directory.
 *  \return True if successful.
 */
bool extract_zip(const std::string &from, const std::string &to, bool recursive,
                 const ZipSafety::Limits& limits)
{
    //Add the zip to the file system
    IFileSystem *file_system = irr_driver->getDevice()->getFileSystem();
    if(!file_system->addFileArchive(from.c_str(),
                                    /*ignoreCase*/false,
                                   /*ignorePath*/false, io::EFAT_ZIP))
    {
        return false;
    }

    // Get the recently added archive, which is necessary to get a
    // list of file in the zip archive.
    io::IFileArchive *zip_archive =
        file_system->getFileArchive(file_system->getFileArchiveCount()-1);
    const io::IFileList *zip_file_list = zip_archive->getFileList();
    if (!zip_file_list ||
        !ZipSafety::withinBudget(zip_file_list->getFileCount(), 0, limits))
    {
        file_system->removeFileArchive(file_system->getAbsolutePath(from.c_str()));
        return false;
    }
    uint64_t expanded_bytes = 0;
    for (u32 i = 0; i < zip_file_list->getFileCount(); ++i)
    {
        if (zip_file_list->isDirectory(i)) continue;
        const uint64_t size = zip_file_list->getFileSize(i);
        const std::string archive_name = zip_file_list->getFullFileName(i).c_str();
        std::string output_name = archive_name;
        if (!recursive)
            output_name = StringUtils::getBasename(output_name);
        if (!ZipSafety::isSafeName(archive_name) ||
            !ZipSafety::isSafeName(output_name) ||
            !ZipSafety::canAddFile(expanded_bytes, size, limits))
        {
            file_system->removeFileArchive(file_system->getAbsolutePath(from.c_str()));
            return false;
        }
        expanded_bytes += size;
    }
    // Copy all files from the zip archive to the destination
    bool error = false;
    for(unsigned int i=0; i<zip_file_list->getFileCount(); i++)
    {
        if(zip_file_list->isDirectory(i)) continue;
        if(zip_file_list->getFileName(i)[0]=='.') continue;
        std::string archive_name = zip_file_list->getFullFileName(i).c_str();
        std::string base = archive_name;
        if (!recursive)
            base = StringUtils::getBasename(base);

        // All paths and sizes were validated before extraction starts.

        Log::debug("addons", "Unzipping file '%s'.", base.c_str());

        IReadFile* src_file =
            zip_archive->createAndOpenFile(archive_name.c_str());
        if(!src_file)
        {
            Log::warn("addons", "Can't read file '%s'. This is ignored, but the addon might not work", base.c_str());
            error = true;
            continue;
        }
        if (src_file->getSize() < 0 ||
            (uint64_t)src_file->getSize() != zip_file_list->getFileSize(i))
        {
            Log::warn("addons", "Archive size mismatch for '%s'.", base.c_str());
            src_file->drop();
            error = true;
            continue;
        }

        std::string file_location = to + "/" + base;
        if (!ZipSafety::isConfinedPath(to, base))
        {
            Log::warn("addons", "Refusing to write through a link in '%s'.", file_location.c_str());
            src_file->drop();
            error = true;
            continue;
        }
        if (recursive)
        {
            const std::string& dir = StringUtils::getPath(file_location);
            file_manager->checkAndCreateDirectoryP(dir);
        }
        if (!ZipSafety::isConfinedPath(to, base))
        {
            Log::warn("addons", "Refusing to write through a link in '%s'.", file_location.c_str());
            src_file->drop();
            error = true;
            continue;
        }
        IWriteFile* dst_file =
            file_system->createAndWriteFile(file_location.c_str());
        if(dst_file == NULL)
        {
            Log::warn("addons", "Couldn't create the file '%s'. The directory might not exist. This is ignored, but the addon might not work.", file_location.c_str());
            error = true;
            continue;
        }

        if (!copyFileBounded(dst_file, src_file, zip_file_list->getFileSize(i)))
        {
            Log::warn("addons", "Could not copy '%s' from archive '%s'. This is ignored, but the addon might not work.",
                      base.c_str(), from.c_str());
            error = true;
        }
        dst_file->drop();
        src_file->drop();
    }
    // Remove the zip from the filesystem to save memory and avoid
    // problem with a name conflict. Note that we have to convert
    // the path using getAbsolutePath, otherwise windows name
    // will not be detected correctly (e.g. if from=c:\...  the
    // stored filename will be c:/..., which then does not match
    // on removing it. getAbsolutePath will convert all \ to /.
    file_system->removeFileArchive(file_system->getAbsolutePath(from.c_str()));

    return !error;
}   // extract_zip
