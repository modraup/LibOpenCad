// SPDX-License-Identifier: MPL-2.0
#include <dlfcn.h>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: dwg_host <plugin.bundle> <output.dwg>\n";
        return 2;
    }
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        std::cerr << "Cannot load " << argv[1] << ": " << dlerror() << '\n';
        return 1;
    }
    using Roundtrip = int (*)(const char *);
    auto roundtrip = reinterpret_cast<Roundtrip>(dlsym(module, "cad_roundtrip"));
    int status = 1;
    if (roundtrip)
        status = roundtrip(argv[2]);
    else
        std::cerr << "Cannot resolve cad_roundtrip: " << dlerror() << '\n';
    if (dlclose(module) != 0) {
        std::cerr << "Cannot unload plugin: " << dlerror() << '\n';
        return 1;
    }
    if (status == 0)
        std::cout << "Verified AC1032 DWG and LINE endpoints in " << argv[2] << '\n';
    return status;
}
