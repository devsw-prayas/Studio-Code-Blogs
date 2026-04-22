#include <cstdint>
#include <cstddef>

void add_limbs(const uint64_t* a, const uint64_t* b, uint64_t* sum, size_t N) {
    uint64_t carry = 0;
    for (size_t i = 0; i < N; ++i) {
        uint64_t t = a[i] + b[i];
        uint64_t s = t + carry;
        carry = (t < a[i]) || (s < t);
        sum[i] = s;
    }
}