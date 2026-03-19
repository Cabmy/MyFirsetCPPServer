#include "Buffer.h"
#include <iostream>
#include <cstring>

Buffer::Buffer()
{
}

Buffer::~Buffer()
{
}

void Buffer::append(const char *str, int size)
{
    if (str == nullptr || size <= 0)
    {
        return;
    }

    buf_.insert(buf_.end(), str, str + size);
}

ssize_t Buffer::size()
{
    if (!buf_.empty() && buf_.back() == '\0')
    {
        return static_cast<ssize_t>(buf_.size() - 1);
    }

    return static_cast<ssize_t>(buf_.size());
}

const char *Buffer::c_str()
{
    if (buf_.empty())
    {
        return "";
    }

    if (buf_.back() != '\0')
    {
        buf_.push_back('\0');
    }

    return buf_.data();
}

void Buffer::clear()
{
    buf_.clear();
}

// 不包含'\n'
void Buffer::getline()
{
    buf_.clear();

    std::string line;
    std::getline(std::cin, line);
    buf_.assign(line.begin(), line.end());
}

void Buffer::setBuf(const char *str)
{
    buf_.clear();
    if (str == nullptr)
    {
        return;
    }

    const size_t len = std::strlen(str);
    buf_.assign(str, str + len);
}