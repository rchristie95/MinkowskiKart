#ifndef HEADER_ZIP_SAFETY_HPP
#define HEADER_ZIP_SAFETY_HPP

#include <stdint.h>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ZipSafety
{
struct Limits
{
    uint64_t entries;
    uint64_t expanded_bytes;
    uint64_t file_bytes;
};

static const Limits ADDON_LIMITS = { 10000, 1ULL * 1024 * 1024 * 1024,
                                     256ULL * 1024 * 1024 };
static const Limits MOBILE_ASSET_LIMITS = { 50000, 8ULL * 1024 * 1024 * 1024,
                                            1ULL * 1024 * 1024 * 1024 };

inline bool isSafeName(const std::string& name)
{
    if (name.empty() || name[0] == '/' || name[0] == '\\' ||
        name.find('\\') != std::string::npos || name.find(':') != std::string::npos ||
        name.find('\0') != std::string::npos)
        return false;
    size_t start = 0;
    while (start <= name.size())
    {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        const std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (part[part.size() - 1] == '.' || part[part.size() - 1] == ' ')
            return false;
#ifdef _WIN32
        std::string device = part.substr(0, part.find('.'));
        for (std::string::iterator c = device.begin(); c != device.end(); ++c)
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
        if (device == "CON" || device == "PRN" || device == "AUX" ||
            device == "NUL" || device == "COM1" || device == "COM2" ||
            device == "COM3" || device == "COM4" || device == "COM5" ||
            device == "COM6" || device == "COM7" || device == "COM8" ||
            device == "COM9" || device == "LPT1" || device == "LPT2" ||
            device == "LPT3" || device == "LPT4" || device == "LPT5" ||
            device == "LPT6" || device == "LPT7" || device == "LPT8" ||
            device == "LPT9") return false;
#endif
        start = end + 1;
    }
    return true;
}

inline bool withinBudget(uint64_t entry_count, uint64_t expanded_bytes,
                         const Limits& limits)
{
    return entry_count <= limits.entries &&
           expanded_bytes <= limits.expanded_bytes;
}

inline bool canAddFile(uint64_t current_bytes, uint64_t file_bytes,
                      const Limits& limits)
{
    return file_bytes <= limits.file_bytes &&
           current_bytes <= limits.expanded_bytes &&
           file_bytes <= limits.expanded_bytes - current_bytes;
}

inline bool hasLinkComponent(const std::string& path)
{
    std::string checked = path;
    while (checked.size() > 1 &&
           (checked[checked.size() - 1] == '/' || checked[checked.size() - 1] == '\\'))
    {
#ifdef _WIN32
        if (checked.size() == 3 && checked[1] == ':') break;
#endif
        checked.erase(checked.size() - 1);
    }
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, checked.c_str(),
                                    -1, NULL, 0);
    if (count <= 0) return true;
    std::wstring wide((size_t)count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, checked.c_str(), -1,
                             &wide[0], count)) return true;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(wide.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;
    FindClose(find);
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    return lstat(checked.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

inline bool isConfinedPath(const std::string& root, const std::string& relative)
{
    if (!isSafeName(relative) || hasLinkComponent(root)) return false;
    std::string current = root;
    size_t start = 0;
    while (start <= relative.size())
    {
        size_t end = relative.find('/', start);
        if (end == std::string::npos) end = relative.size();
        current += "/" + relative.substr(start, end - start);
        if (hasLinkComponent(current)) return false;
        start = end + 1;
    }
    return true;
}

} // namespace ZipSafety

#endif
