#include "app/Application.h"

#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    app::Application app;

    try {
        // argv[1] 可选：入口配置文件路径（默认 configs/main.json）
        app.run(argc > 1 ? argv[1] : nullptr);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
