#include "jvm_midlet_app.hpp"
#include "j2me_port/J2MECompat.hpp"
#include "sdl_image_decoder.hpp"

#include <SDL.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

class SdlJvmHost final : public jvmpoc::JvmHost {
public:
    SdlJvmHost(int width, int height, int scale)
        : width_(width), height_(height), scale_(scale) {
        window_ = SDL_CreateWindow(
            "jvm-poc SDL demo",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            width_ * scale_,
            height_ * scale_,
            SDL_WINDOW_SHOWN);
        if (window_ == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }

        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (renderer_ == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_RenderSetLogicalSize(renderer_, width_, height_);

        texture_ = SDL_CreateTexture(
            renderer_,
            SDL_PIXELFORMAT_RGB565,
            SDL_TEXTUREACCESS_STREAMING,
            width_,
            height_);
        if (texture_ == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }

        start_ = std::chrono::steady_clock::now();
    }

    ~SdlJvmHost() override {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
        }
    }

    int screenWidth() const override { return width_; }
    int screenHeight() const override { return height_; }

    uint32_t millis() const override {
        auto elapsed = std::chrono::steady_clock::now() - start_;
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    void present(const uint16_t* pixels, int width, int height) override {
        if (pixels == nullptr || width != width_ || height != height_) {
            return;
        }
        SDL_UpdateTexture(texture_, nullptr, pixels, width_ * static_cast<int>(sizeof(uint16_t)));
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

private:
    int width_ = 0;
    int height_ = 0;
    int scale_ = 1;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    std::chrono::steady_clock::time_point start_;
};

void printUnknownCalls(const jvmpoc::ExecutionTrace& trace) {
    if (trace.unknownMethodCalls.empty()) {
        return;
    }
    std::cout << "unknown method calls:\n";
    for (const jvmpoc::UnknownMethodCall& call : trace.unknownMethodCalls) {
        std::cout << "  " << call.methodName;
        if (call.nooped) {
            std::cout << " noop";
        }
        if (!call.result.empty()) {
            std::cout << " -> " << call.result;
        }
        std::cout << "\n";
    }
}

void printUsage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " [--assets dir] <midlet-class/name> <class-file> [class-file...]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 2;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }

    try {
        jvmpoc::installSdlImageDecoder();

        std::string assetsDir;
        int argIndex = 1;
        while (argIndex < argc) {
            std::string arg = argv[argIndex];
            if (arg == "--assets") {
                if (argIndex + 1 >= argc) {
                    printUsage(argv[0]);
                    SDL_Quit();
                    return 2;
                }
                assetsDir = argv[argIndex + 1];
                argIndex += 2;
                continue;
            }
            break;
        }
        if (!assetsDir.empty()) {
            port::setResourceRoot(assetsDir);
        }

        if (argc - argIndex < 2) {
            printUsage(argv[0]);
            SDL_Quit();
            return 2;
        }

        constexpr int width = 240;
        constexpr int height = 320;
        constexpr int scale = 2;

        SdlJvmHost host(width, height, scale);
        jvmpoc::JvmMidletApp app(host);

        std::vector<std::string> classFiles;
        for (int i = argIndex + 1; i < argc; ++i) {
            classFiles.push_back(argv[i]);
        }
        app.loadClasses(classFiles);
        const jvmpoc::ExecutionTrace& startTrace = app.start(argv[argIndex]);
        printUnknownCalls(startTrace);
        (void)app.render();

        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
            }

            (void)app.render();
            SDL_Delay(16);
        }
    } catch (const std::exception& e) {
        std::cerr << "jvm-poc-sdl-demo: " << e.what() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Quit();
    return 0;
}
