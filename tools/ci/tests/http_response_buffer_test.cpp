#include "online/http_response_buffer.hpp"
#include <cassert>

int main()
{
    std::string data;
    Online::HTTPResponseBuffer memory = { &data, NULL, 0, 5, false };
    assert(memory.write("abc", 1, 3) == 3);
    assert(memory.write("de", 1, 2) == 2);
    assert(data == "abcde");
    assert(memory.write("f", 1, 1) == 0);
    assert(memory.limit_exceeded && data == "abcde" && memory.received == 5);

    Online::HTTPResponseBuffer overflow = { &data, NULL, 0, 100, false };
    assert(overflow.write("", std::numeric_limits<size_t>::max(), 2) == 0);
    assert(overflow.limit_exceeded && overflow.received == 0);

    FILE* file = tmpfile();
    assert(file);
    Online::HTTPResponseBuffer disk = { NULL, file, 0, 5, false };
    assert(disk.write("abc", 1, 3) == 3);
    assert(disk.write("def", 1, 3) == 0);
    assert(disk.limit_exceeded && disk.received == 3);
    rewind(file);
    char stored[4] = {};
    assert(fread(stored, 1, 4, file) == 3);
    assert(std::string(stored) == "abc");
    fclose(file);
}
