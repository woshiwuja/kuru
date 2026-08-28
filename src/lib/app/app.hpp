#include <memory>
#include "../core/core.hpp"
#include <string>
namespace KR {
    struct App{
        int width= 800;
        int height = 600;
        std::string title = "New Kuru App";
        std::unique_ptr<Core> core = nullptr;
        App(){
            core = std::make_unique<Core>();
            core->width = width;
            core->height = height;
        }
    };

}
