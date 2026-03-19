#pragma once
#include <string>
#include <vector>

class Buffer
{
private:
    std::vector<char> buf_;

public:
    Buffer();
    ~Buffer();

    void append(const char *str, int size);
    ssize_t size();
    const char *c_str();
    void clear();
    void getline();
    void setBuf(const char *str);
};