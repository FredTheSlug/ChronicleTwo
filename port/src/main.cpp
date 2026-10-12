#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "dataread.hpp"
#include "exitcodes.hpp"
#include "gfx/gfx.hpp"
#include "identity.hpp"
#include "mainloop.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/display.hpp"
#include "platform/input.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"
#include "sce/threads.hpp"

int VSyncCallBack(int field);

namespace {

constexpr float kLogicalWidth = 512.0f;
constexpr float kLogicalHeight = 480.0f;

struct Options {
    bool                  headless = false;
    bool                  offscreen = false;
    int                   frames = 0;
    std::filesystem::path screenshot;
    std::filesystem::path input;
};

[[noreturn]] void Usage(const char *program) {
    std::fprintf(stderr,
                 "usage: %s [--data DIR] [--save DIR] [--headless] [--offscreen] [--frames N]\n"
                 "          [--screenshot PATH] [--input SCRIPT]\n"
                 "  --data DIR         the extracted game data (default: DC2_DATA, then ./data, then data/ beside\n"
                 "                     the executable)\n"
                 "  --save DIR         saves and the pipeline cache (default: DC2_SAVE, then ./save, then save/\n"
                 "                     beside the executable)\n"
                 "  --headless         render offscreen (SDL offscreen driver, VK_EXT_headless_surface)\n"
                 "  --offscreen        --headless without a Vulkan surface: render to an image only\n"
                 "  --frames N         exit after frame N of the game's main loop (N a positive integer)\n"
                 "  --screenshot PATH  with --frames: write the last frame to PATH as a PNG on exit\n"
                 "  --input SCRIPT     drive the pads named in SCRIPT from an input script\n",
                 program);
    std::exit(kExitUsage);
}

const char *OptionValue(int argc, const char **argv, int &i) {
    if (i + 1 >= argc) {
        Usage(argv[0]);
    }
    return argv[++i];
}

int ParseFrames(std::string_view text, const char *program) {
    int         frames = 0;
    const char *last = text.data() + text.size();
    const auto [end, ec] = std::from_chars(text.data(), last, frames);
    if (ec != std::errc() || end != last || frames <= 0) {
        Usage(program);
    }
    return frames;
}

Options ParseOptions(int argc, const char **argv) {
    Options options;
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--offscreen") {
            options.headless = true;
            options.offscreen = true;
        } else if (arg == "--frames") {
            options.frames = ParseFrames(OptionValue(argc, argv, i), argv[0]);
        } else if (arg == "--screenshot") {
            options.screenshot = PathsFromUtf8(OptionValue(argc, argv, i));
            if (options.screenshot.empty()) {
                Usage(argv[0]);
            }
        } else if (arg == "--input") {
            options.input = PathsFromUtf8(OptionValue(argc, argv, i));
            if (options.input.empty()) {
                Usage(argv[0]);
            }
        } else {
            Usage(argv[0]);
        }
    }
    if (!options.screenshot.empty() && options.frames == 0) {
        Usage(argv[0]);
    }
    return options;
}

void ReportPipelineProgress(std::uint32_t done, std::uint32_t total) {
    static std::uint32_t    reported = 0;
    constexpr std::uint32_t kSteps = 4;
    if (total == 0) {
        return;
    }
    std::uint32_t step = done * kSteps / total;
    if (done == 0 || step > reported) {
        reported = step;
        std::fprintf(stderr, "compiling shaders %u/%u\n", done, total);
    }
}

gfx::PresentMode PresentMode(ConfigPresentMode mode) {
    switch (mode) {
        case ConfigPresentMode::Mailbox:
            return gfx::PresentMode::Mailbox;
        case ConfigPresentMode::Immediate:
            return gfx::PresentMode::Immediate;
        default:
            return gfx::PresentMode::Fifo;
    }
}

// A closed window ends the process here: the game has no stop request to ask its loop to return.
void PumpHost() {
    if (!WindowPollEvents()) {
        std::exit(kExitOk);
    }
    DisplayPump();
    InputPoll();
    InputScriptApply(mgPortFrameCount());
}

void ApplyConfigChange(const HostSettings &before, const HostSettings &after) {
    if (before.tick_rate != after.tick_rate) {
        ClockSetTickRate(after.tick_rate);
    }
    InputApplyConfig(after);
}

int Run(int argc, const char **argv) {
    GameSetIdentity();
    argc = PathsConsumeArgs(argc, argv);
    const Options options = ParseOptions(argc, argv);
    InputScript   input_script;
    if (!options.input.empty()) {
        std::string error;
        if (!InputScriptLoad(options.input, input_script, error)) {
            std::fprintf(stderr, "bad input script: %s\n", error.c_str());
            std::exit(kExitUsage);
        }
    }
    ConfigLoad();
    const HostSettings &settings = SettingsGet();
    const bool          offscreen = options.offscreen || (options.headless && !gfx::HeadlessSurfaceAvailable());

    DisplaySetOverrides(0, 0, options.headless);
    WindowConfig window = DisplayWindowConfig(settings);
    window.vulkan = !offscreen;
    WindowInit(window);
    InputInit();
    InputScriptInstall(std::move(input_script));

    gfx::RendererConfig renderer;
    renderer.app_name = "Dark Chronicle";
    renderer.logical_width = kLogicalWidth;
    renderer.logical_height = kLogicalHeight;
    renderer.display_aspect = 4.0f / 3.0f;
    renderer.present_mode = PresentMode(settings.present_mode);
    renderer.pipeline_cache = PathsSaveRoot() / "pipeline_cache.bin";
    renderer.progress = ReportPipelineProgress;
    renderer.offscreen = offscreen;
    gfx::RendererInit(WindowHandle(), renderer);

    ClockSetTickRate(settings.tick_rate);
    ClockSetUnbounded(options.headless);
    ClockAddPumpHook(PumpHost);
    ClockSetTickCallback(VSyncCallBack);
    ConfigSetChangeHook(ApplyConfigChange);

    MainThreadPriority = 10;
    KzThreadsSetMainPriority(MainThreadPriority);
    InitCDFile();
    if (options.frames > 0) {
        mgPortExitAfterFrames(options.frames, options.screenshot);
    }
    MainLoop();

    ClockRemovePumpHook(PumpHost);
    ConfigSetChangeHook(nullptr);
    InputShutdown();
    gfx::RendererShutdown();
    WindowShutdown();
    return kExitOk;
}

} // namespace

PORT_OVERRIDE int main(int argc, const char **argv) {
    return Run(argc, argv);
}

#ifdef _WIN32
int wmain(int argc, wchar_t **wide_argv) {
    std::vector<std::string>  args;
    std::vector<const char *> argv;
    for (int i = 0; i < argc; ++i) {
        args.push_back(PathsDisplay(std::filesystem::path(wide_argv[i])));
    }
    for (const std::string &arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    return Run(argc, argv.data());
}
#endif
