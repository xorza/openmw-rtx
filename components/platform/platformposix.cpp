#include "platform.hpp"

namespace Platform
{
    // Linux's limit on open files is the process's own, which nothing here needs to raise.
    void init() {}
}
