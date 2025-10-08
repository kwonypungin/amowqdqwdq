#include "engine.hpp"
#include <iostream>

int main() {
    Engine e;
    int rc = e.run_once();
    std::cout << "engine rc=" << rc << "\n";
    return rc;
}

