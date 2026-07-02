#include <iostream>
class Greeter { public: void hello() { std::cout << "hello" << 1; } };
int main() { Greeter g; g.hello(); return 0; }
