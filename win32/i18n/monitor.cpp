#include "monitor.h"

namespace fcitx::win32::i18n {
TranslationMonitor::TranslationMonitor(std::filesystem::path settings,
                                       std::filesystem::path resources)
    : state_(std::make_shared<State>()),
      latest_(std::make_shared<Translator>()), settings_(std::move(settings)),
      resources_(std::move(resources)) {}

void TranslationMonitor::reset() {
    state_ = std::make_shared<State>();
    nextCheck_ = 0;
}

void TranslationMonitor::setLanguageReader(
    std::function<bool(ipc::UiLanguage &)> reader) {
    reset();
    reader_ = std::move(reader);
}

DWORD WINAPI TranslationMonitor::run(void *argument) {
    auto *task = static_cast<Task *>(argument);
    Translation result;
    try {
        ipc::GlobalSettings settings;
        ipc::UiLanguage language = ipc::UiLanguage::System;
        const bool loaded =
            task->reader ? task->reader(language)
                         : readSettings(task->settings.empty() ? settingsPath()
                                                               : task->settings,
                                        settings);
        if (loaded) {
            if (!task->reader)
                language = resolveLanguage(settings.language);
            if (language != task->previous->language())
                result =
                    std::make_shared<Translator>(language, task->resources);
        }
    } catch (...) {
    }
    {
        std::lock_guard lock(task->state->mutex);
        task->state->result = std::move(result);
        task->state->ready = true;
        task->state->running = false;
    }
    const auto module = task->module;
    delete task;
    // No return into DLL code after releasing the worker's module reference.
    FreeLibraryAndExitThread(module, 0);
    return 0;
}

Translation TranslationMonitor::poll() {
    Translation updated;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->ready) {
            state_->ready = false;
            updated = std::move(state_->result);
            if (updated)
                latest_ = updated;
        }
        if (state_->running || GetTickCount64() < nextCheck_)
            return updated;
        state_->running = true;
    }
    nextCheck_ = GetTickCount64() + 1000;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&run), &module)) {
        std::lock_guard lock(state_->mutex);
        state_->running = false;
        return updated;
    }
    auto *task = new (std::nothrow)
        Task{state_, latest_, module, settings_, resources_, reader_};
    HANDLE thread =
        task ? CreateThread(nullptr, 0, run, task, 0, nullptr) : nullptr;
    if (thread)
        CloseHandle(thread);
    else {
        delete task;
        FreeLibrary(module);
        std::lock_guard lock(state_->mutex);
        state_->running = false;
    }
    return updated;
}
} // namespace fcitx::win32::i18n
