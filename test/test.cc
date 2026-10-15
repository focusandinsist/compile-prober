#include <iostream>
#include <tuple>

#include "fixture/detail.h"

namespace sample {

int multiply(int left, int right) { return left * right; }

struct Calculator {
  int calculate(int value) const;
};

int Calculator::calculate(int value) const {
  return multiply(value, fixture::offset());
}

template <typename T> T identity(T value) { return value; }

} // namespace sample

int main() {
  const auto values = std::make_tuple(1, 2, 3, 4);
  sample::Calculator calculator;
  std::cout << sample::identity(std::get<2>(values)) +
                   calculator.calculate(2)
            << '\n';
}
