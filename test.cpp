#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <type_traits>

template <typename T> class Test final {
  T fib(T X) {
    if (X <= 1)
      return 1;
    return fib(X - 1) + fib(X - 2);
  }

  T gcd(T A, T B) {
    if (B == 0)
      return A;
    return gcd(B, A % B);
  }

  // std::minstd_rand RNG;
  // std::uniform_int_distribution<T> Distrib{std::numeric_limits<T>::min()};
  T Idx = 0;

  T generate() {
    Idx = Idx * 97 + 1;
    return Idx;
    // return Distrib(RNG);
  }

public:
  void run() {
    using U = std::conditional_t<
        std::is_same_v<int8_t, T> || std::is_same_v<uint8_t, T>,
        std::conditional_t<std::is_same_v<int8_t, T>, int16_t, uint16_t>, T>;
    constexpr int Count = 1000;
    for (int I = 0; I < Count; ++I) {
      T V = generate() % 20;
      std::cout << "fib(" << U(V) << ") = " << U(T(fib(V))) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << "gcd(" << U(A) << ", " << U(B) << ") = " << U(T(gcd(A, B)))
                << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " + " << U(B) << " = " << U(T(A + B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " - " << U(B) << " = " << U(T(A - B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " * " << U(B) << " = " << U(T(A * B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      if (B)
        std::cout << U(A) << " / " << U(B) << " = " << U(T(A / B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      if (B)
        std::cout << U(A) << " % " << U(B) << " = " << U(T(A % B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate() % 5;
      T B = generate() % 5;
      std::cout << U(A) << " == " << U(B) << " = " << (A == B) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate() % 5;
      T B = generate() % 5;
      std::cout << U(A) << " != " << U(B) << " = " << (A != B) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " < " << U(B) << " = " << (A < B) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate() % 5;
      T B = generate() % 5;
      std::cout << U(A) << " <= " << U(B) << " = " << (A <= B) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " > " << U(B) << " = " << (A > B) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate() % 5;
      T B = generate() % 5;
      std::cout << U(A) << " >= " << U(B) << " = " << (A >= B) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " & " << U(B) << " = " << U(T(A & B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " | " << U(B) << " = " << U(T(A | B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate();
      std::cout << U(A) << " ^ " << U(B) << " = " << U(T(A ^ B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate() % (sizeof(T) * 8);
      std::cout << U(A) << " << " << U(B) << " = " << U(T(A << B)) << std::endl;
    }
    for (int I = 0; I < Count; ++I) {
      T A = generate();
      T B = generate() % (sizeof(T) * 8);
      std::cout << U(A) << " >> " << U(B) << " = " << U(T(A >> B)) << std::endl;
    }
  }
};

int main() {
  puts("hello");
  Test<int8_t>{}.run();
  Test<uint8_t>{}.run();
  Test<int16_t>{}.run();
  Test<uint16_t>{}.run();
  Test<int32_t>{}.run();
  Test<uint32_t>{}.run();
  Test<int64_t>{}.run();
  Test<uint64_t>{}.run();
  return 0;
}
