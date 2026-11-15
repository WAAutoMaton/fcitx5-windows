#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <zhuyin.h>

namespace {
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() /
               ("fcitx5-zhuyin-" + std::to_string(random()) + "-" +
                std::to_string(random()));
        if (!std::filesystem::create_directory(path)) {
            throw std::runtime_error("Cannot create isolated user directory");
        }
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error(
                "Usage: zhuyin_model_probe <model directory>");
        }
        TemporaryDirectory user;
        for (int pass = 0; pass < 2; ++pass) {
            std::unique_ptr<zhuyin_context_t, decltype(&zhuyin_fini)> context(
                zhuyin_init(argv[1], user.path.string().c_str()), zhuyin_fini);
            if (!context) {
                throw std::runtime_error("Cannot load Zhuyin model");
            }
            zhuyin_set_options(context.get(),
                               USE_TONE | FORCE_TONE | DYNAMIC_ADJUST);
            zhuyin_set_chewing_scheme(context.get(), ZHUYIN_STANDARD);
            zhuyin_load_phrase_library(context.get(), USER_DICTIONARY);
            std::unique_ptr<zhuyin_instance_t, decltype(&zhuyin_free_instance)>
                instance(zhuyin_alloc_instance(context.get()),
                         zhuyin_free_instance);
            if (!instance ||
                !zhuyin_parse_more_chewings(instance.get(), "su3") ||
                !zhuyin_guess_sentence(instance.get())) {
                throw std::runtime_error("Cannot produce Zhuyin sentence");
            }
            if (!zhuyin_train(instance.get()) || !zhuyin_save(context.get())) {
                throw std::runtime_error("Cannot train/save Zhuyin model");
            }
            for (const auto &entry :
                 std::filesystem::directory_iterator(user.path)) {
                if (entry.path().extension() == ".tmp") {
                    throw std::runtime_error(
                        "Saving did not replace the old model");
                }
            }
            for (const auto *name :
                 {"user.conf", "user_bigram.db", "user.bin"}) {
                if (!std::filesystem::file_size(user.path / name)) {
                    throw std::runtime_error("Saved model file is empty");
                }
            }
        }
        std::cout << "PASS: Zhuyin model trained, saved twice and reloaded\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
