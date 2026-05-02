#include "extracted_midlet.hpp"
#include "jvm_midlet_app.hpp"
#include "j2me_port/J2MECompat.hpp"
#include "stb_image_decoder.hpp"

#include <SDL.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
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

    void sleepMillis(uint32_t ms) const override {
        SDL_Delay(ms);
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

bool isIgnorableUnknownCall(const jvmpoc::UnknownMethodCall& call) {
    return call.methodName == "java/io/PrintStream.println(Ljava/lang/String;)V";
}

bool hasMeaningfulUnknownCalls(const jvmpoc::ExecutionTrace& trace) {
    for (const jvmpoc::UnknownMethodCall& call : trace.unknownMethodCalls) {
        if (!isIgnorableUnknownCall(call)) {
            return true;
        }
    }
    return false;
}

void printMeaningfulUnknownCalls(const jvmpoc::ExecutionTrace& trace) {
    bool printedHeader = false;
    for (const jvmpoc::UnknownMethodCall& call : trace.unknownMethodCalls) {
        if (isIgnorableUnknownCall(call)) {
            continue;
        }
        if (!printedHeader) {
            std::cout << "unknown method calls:\n";
            printedHeader = true;
        }
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

void printSuspiciousFrame(const jvmpoc::ExecutionTrace& trace, int blankFrames) {
    std::cout << "suspicious frame: blankFrames=" << blankFrames;
    if (!trace.currentDisplayableClass.empty()) {
        std::cout << " displayable=" << trace.currentDisplayableClass;
    }
    std::cout << "\n";

    if (trace.stepLimitHit) {
        std::cout << "  step limit hit\n";
    }
    if (!trace.currentDisplayableFields.empty()) {
        std::cout << "  displayable fields:";
        for (const std::string& field : trace.currentDisplayableFields) {
            std::cout << " " << field;
        }
        std::cout << "\n";
    }
    if (!trace.stackSnapshot.empty()) {
        std::cout << "  stack snapshot:\n";
        for (const std::string& frame : trace.stackSnapshot) {
            std::cout << "    " << frame << "\n";
        }
    }
    if (!trace.suspendedTasks.empty()) {
        std::cout << "  suspended tasks:\n";
        for (const std::string& task : trace.suspendedTasks) {
            std::cout << "    " << task << "\n";
        }
    }
    printMeaningfulUnknownCalls(trace);
}

bool shouldPrintSuspiciousFrame(const jvmpoc::ExecutionTrace& trace, int blankFrames) {
    if (trace.stepLimitHit || hasMeaningfulUnknownCalls(trace)) {
        return true;
    }
    if (blankFrames < 30) {
        return false;
    }
    return blankFrames == 30 || blankFrames % 120 == 0;
}

std::map<std::string, std::string> fieldMap(const jvmpoc::ExecutionTrace& trace) {
    std::map<std::string, std::string> fields;
    for (const std::string& field : trace.currentDisplayableFields) {
        size_t split = field.find('=');
        if (split == std::string::npos) {
            continue;
        }
        fields[field.substr(0, split)] = field.substr(split + 1);
    }
    return fields;
}

std::string fieldOrMissing(const std::map<std::string, std::string>& fields, const std::string& name) {
    auto it = fields.find(name);
    return it == fields.end() ? "<unset>" : it->second;
}

std::string trackedStateKey(const jvmpoc::ExecutionTrace& trace) {
    std::map<std::string, std::string> fields = fieldMap(trace);
    return trace.currentDisplayableClass +
        " screen=" + fieldOrMissing(fields, "screen") +
        " state=" + fieldOrMissing(fields, "state") +
        " ani_step=" + fieldOrMissing(fields, "ani_step") +
        " m_mode=" + fieldOrMissing(fields, "m_mode") +
        " p_mode=" + fieldOrMissing(fields, "p_mode") +
        " game_on=" + fieldOrMissing(fields, "game_on") +
        " msg=" + fieldOrMissing(fields, "msg");
}

void printStateTransition(const jvmpoc::ExecutionTrace& trace) {
    if (trace.currentDisplayableClass.empty()) {
        return;
    }
    std::cout << "state transition: " << trackedStateKey(trace) << "\n";
}

void printStalledState(const jvmpoc::ExecutionTrace& trace, int repeatedFrames) {
    std::cout << "stalled state: repeatedFrames=" << repeatedFrames
              << " " << trackedStateKey(trace) << "\n";
    if (trace.stepLimitHit) {
        std::cout << "  step limit hit\n";
    }
    if (!trace.fieldWrites.empty()) {
        std::cout << "  field writes:\n";
        for (const jvmpoc::FieldWrite& write : trace.fieldWrites) {
            std::cout << "    " << write.methodLabel << " pc=" << write.pc
                      << " " << write.fieldName << "=" << write.value.text << "\n";
        }
    }
    if (!trace.imageLoads.empty()) {
        std::cout << "  image loads:\n";
        for (const jvmpoc::ImageLoad& load : trace.imageLoads) {
            std::cout << "    " << load.methodLabel << " pc=" << load.pc
                      << " source=" << load.source
                      << " size=" << load.width << "x" << load.height << "\n";
        }
    }
    if (!trace.stackSnapshot.empty()) {
        std::cout << "  stack snapshot:\n";
        for (const std::string& frame : trace.stackSnapshot) {
            std::cout << "    " << frame << "\n";
        }
    }
    if (!trace.suspendedTasks.empty()) {
        std::cout << "  suspended tasks:\n";
        for (const std::string& task : trace.suspendedTasks) {
            std::cout << "    " << task << "\n";
        }
    }
    printMeaningfulUnknownCalls(trace);
}

bool shouldPrintStalledState(const jvmpoc::ExecutionTrace& trace, int repeatedFrames) {
    if (trace.currentDisplayableClass != "GameScreen") {
        return false;
    }

    std::map<std::string, std::string> fields = fieldMap(trace);
    auto screenIt = fields.find("screen");
    if (screenIt == fields.end() || screenIt->second != "333") {
        return false;
    }

    return repeatedFrames == 30 || repeatedFrames % 120 == 0;
}

void printUsage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " [--midlet class/name] <extracted-midlet-root>\n"
              << "       " << argv0 << " [--assets dir] <midlet-class/name> <class-file> [class-file...]\n";
}

std::optional<int> midpKeyCode(SDL_Keycode key) {
    switch (key) {
        case SDLK_UP: return -1;
        case SDLK_DOWN: return -2;
        case SDLK_LEFT: return -3;
        case SDLK_RIGHT: return -4;
        case SDLK_KP_0: return '0';
        case SDLK_KP_1: return '1';
        case SDLK_KP_2: return '2';
        case SDLK_KP_3: return '3';
        case SDLK_KP_4: return '4';
        case SDLK_KP_5: return '5';
        case SDLK_KP_6: return '6';
        case SDLK_KP_7: return '7';
        case SDLK_KP_8: return '8';
        case SDLK_KP_9: return '9';
        case SDLK_KP_PERIOD: return -6;
        case SDLK_KP_ENTER: return -7;
        default: return std::nullopt;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 2;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }

    try {
        jvmpoc::installStbImageDecoder();

        std::string assetsDir;
        std::string midletOverride;
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
            } else if (arg == "--midlet") {
                if (argIndex + 1 >= argc) {
                    printUsage(argv[0]);
                    SDL_Quit();
                    return 2;
                }
                midletOverride = argv[argIndex + 1];
                argIndex += 2;
                continue;
            }
            break;
        }

        if (argc - argIndex < 1) {
            printUsage(argv[0]);
            SDL_Quit();
            return 2;
        }

        constexpr int width = 240;
        constexpr int height = 320;
        constexpr int scale = 2;

        SdlJvmHost host(width, height, scale);
        jvmpoc::JvmMidletApp app(host);

        std::string midletClass;
        if (argc - argIndex == 1 && jvmpoc::isDirectory(argv[argIndex])) {
            jvmpoc::ExtractedMidlet extracted = jvmpoc::loadExtractedMidlet(argv[argIndex], midletOverride);
            port::setResourceRoot(assetsDir.empty() ? extracted.root : assetsDir);
            app.setClasses(std::move(extracted.classes));
            midletClass = extracted.midletClass;
        } else {
            if (argc - argIndex < 2) {
                printUsage(argv[0]);
                SDL_Quit();
                return 2;
            }
            midletClass = argv[argIndex];
            if (!midletOverride.empty()) {
                midletClass = midletOverride;
            }
            if (!assetsDir.empty()) {
                port::setResourceRoot(assetsDir);
            }
            std::vector<std::string> classFiles;
            for (int i = argIndex + 1; i < argc; ++i) {
                classFiles.push_back(argv[i]);
            }
            app.loadClasses(classFiles);
        }
        const jvmpoc::ExecutionTrace& startTrace = app.start(midletClass);
        printUnknownCalls(startTrace);
        const jvmpoc::ExecutionTrace& firstRenderTrace = app.render();
        std::string lastStateKey = trackedStateKey(firstRenderTrace);
        int repeatedStateFrames = 0;
        if (!lastStateKey.empty()) {
            printStateTransition(firstRenderTrace);
        }

        bool running = true;
        int blankFrames = 0;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                    std::optional<int> keyCode = midpKeyCode(event.key.keysym.sym);
                    if (keyCode.has_value()) {
                        host.handlePress(*keyCode);
                    }
                } else if (event.type == SDL_KEYUP) {
                    std::optional<int> keyCode = midpKeyCode(event.key.keysym.sym);
                    if (keyCode.has_value()) {
                        host.handleRelease(*keyCode);
                    }
                }
            }

            const jvmpoc::ExecutionTrace& renderTrace = app.render();
            std::string stateKey = trackedStateKey(renderTrace);
            if (!stateKey.empty()) {
                if (stateKey != lastStateKey) {
                    printStateTransition(renderTrace);
                    lastStateKey = stateKey;
                    repeatedStateFrames = 0;
                } else {
                    ++repeatedStateFrames;
                    if (shouldPrintStalledState(renderTrace, repeatedStateFrames)) {
                        printStalledState(renderTrace, repeatedStateFrames);
                    }
                }
            } else {
                lastStateKey.clear();
                repeatedStateFrames = 0;
            }
            const bool suspiciousFrame = renderTrace.graphicsOps.empty() &&
                (renderTrace.stepLimitHit ||
                 !renderTrace.currentDisplayableClass.empty() ||
                 !renderTrace.unknownMethodCalls.empty() ||
                 !renderTrace.suspendedTasks.empty());
            if (suspiciousFrame) {
                ++blankFrames;
                if (shouldPrintSuspiciousFrame(renderTrace, blankFrames)) {
                    printSuspiciousFrame(renderTrace, blankFrames);
                }
            } else {
                blankFrames = 0;
            }
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
