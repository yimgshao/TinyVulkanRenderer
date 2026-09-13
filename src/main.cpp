#include "app/Application.h"

#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <utility>

int main() {
    try {
        const auto exampleShaders =
            std::filesystem::path(__FILE__).parent_path().parent_path() /
            "examples/user_shaders";

        app::ConfigLoader configLoader;
        configLoader.addCustomShaderSearchPath(exampleShaders);

        app::Application app(std::move(configLoader));
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
