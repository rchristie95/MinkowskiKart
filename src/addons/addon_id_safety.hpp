#ifndef HEADER_ADDON_ID_SAFETY_HPP
#define HEADER_ADDON_ID_SAFETY_HPP

#include <string>

namespace AddonIdSafety
{
inline bool isValid(const std::string& id)
{
    if (id.empty() || id.size() > 128 || id == "." || id == "..")
        return false;
    for (std::string::const_iterator c = id.begin(); c != id.end(); ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
            return false;
    return true;
}
}

#endif
