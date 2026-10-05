#ifndef HEADER_URL_LAUNCHER_HPP
#define HEADER_URL_LAUNCHER_HPP

#include <errno.h>
#include <stdlib.h>
#include <string>

#if !defined(_WIN32) && !defined(IOS_STK)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace OnlineURL
{
inline bool isAllowedWebURL(const std::string& url)
{
    if (url.size() < 8 || url.size() > 8192 ||
        url.find('\0') != std::string::npos)
        return false;
    for (std::string::const_iterator c = url.begin(); c != url.end(); ++c)
        if ((unsigned char)*c <= 0x20 || (unsigned char)*c == 0x7f)
            return false;
    std::string scheme = url.substr(0, url.find(':'));
    for (std::string::iterator c = scheme.begin(); c != scheme.end(); ++c)
        if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    if ((scheme != "http" && scheme != "https") || url.compare(scheme.size(), 3, "://") != 0)
        return false;
    const size_t host_start = scheme.size() + 3;
    const size_t host_end = url.find_first_of("/?#", host_start);
    return host_start < (host_end == std::string::npos ? url.size() : host_end);
}

inline bool launchURL(const char* program, const std::string& url)
{
    if (!isAllowedWebURL(url)) return false;
    pid_t child = fork();
    if (child < 0) return false;
    if (child == 0)
    {
        const char* system_lib_path = getenv("SYSTEM_LD_LIBRARY_PATH");
        if (system_lib_path) setenv("LD_LIBRARY_PATH", system_lib_path, 1);
        execlp(program, program, url.c_str(), (char*)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
}
#endif

#endif
