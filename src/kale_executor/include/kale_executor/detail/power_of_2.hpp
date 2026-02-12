#pragma once

#include <cstddef>

namespace kale::executor::detail {

constexpr std::size_t round_up_to_power_of_2(std::size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if (SIZE_MAX > 0xFFFFFFFF)
    n |= n >> 32;
#endif
    return n + 1;
}

}  // namespace kale::executor::detail
