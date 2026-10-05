#ifndef HEADER_HTTP_RESPONSE_BUFFER_HPP
#define HEADER_HTTP_RESPONSE_BUFFER_HPP

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace Online
{
/** A bounded sink shared by in-memory and on-disk curl responses. */
struct HTTPResponseBuffer
{
    std::string* buffer;
    FILE* file;
    uint64_t received;
    uint64_t limit;
    bool limit_exceeded;

    size_t write(const char* contents, size_t size, size_t nmemb)
    {
        if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size)
        {
            limit_exceeded = true;
            return 0;
        }
        const size_t bytes = size * nmemb;
        if (received > limit || bytes > limit - received)
        {
            limit_exceeded = true;
            return 0;
        }
        if (file)
        {
            const size_t written = fwrite(contents, 1, bytes, file);
            received += written;
            return written;
        }
        // Exceptions must never cross libcurl's C callback boundary.
        try
        {
            buffer->append(contents, bytes);
        }
        catch (...)
        {
            return 0;
        }
        received += bytes;
        return bytes;
    }
};
}
#endif
