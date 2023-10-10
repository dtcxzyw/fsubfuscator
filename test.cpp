#include <cstdint>
int8_t gcd(int8_t a, int8_t b) { return b ? gcd(b, a % b) : a; }
