#include <iostream>
#include "src/bazel_link_bug_example/foo.h"

int main(int argc, char** argv) {
  std::cout << "Foo: " << Foo(1) << "\n";
  std::cout << "Bar: " << Bar(1) << "\n";
}
