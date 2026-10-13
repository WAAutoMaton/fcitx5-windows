#include "../i18n/translator.h"
#include "../ipc/service.h"
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    namespace ipc = fcitx::win32::ipc;
    namespace i18n = fcitx::win32::i18n;
    if (argc != 2)
        return 1;
    const std::filesystem::path file(argv[1]);
    ipc::GlobalSettings saved;
    if (!i18n::readSettings(file, saved))
        return 2;
    const auto active = i18n::resolveLanguage(saved.language);
    i18n::Translator translator(active);
    ipc::InputMethodsReply catalog;
    catalog.selection = {7, {"keyboard-us", "pinyin"}};
    catalog.methods = {{"keyboard-us",
                        translator.text("input-method", "English Keyboard"), "",
                        "A", "keyboard", true, true},
                       {"pinyin", translator.text("input-method", "Pinyin"), "",
                        "P", "pinyin", true, false},
                       {"wbx", "Wubi", "", "W", "table", true, false}};
    bool prediction = false;
    std::string prevPage = "Up\nminus";
    ipc::ServiceSecurity security;
    for (;;) {
        HANDLE pipe = CreateNamedPipeW(
            ipc::pipeName().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1, ipc::kMaxPayloadSize, ipc::kMaxPayloadSize, 0, security.get());
        if (pipe == INVALID_HANDLE_VALUE)
            return 3;
        OVERLAPPED connect{};
        connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const auto connected = ConnectNamedPipe(pipe, &connect);
        const auto error = connected ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_IO_PENDING) {
            DWORD bytes = 0;
            WaitForSingleObject(connect.hEvent, INFINITE);
            if (!GetOverlappedResult(pipe, &connect, &bytes, FALSE))
                return 4;
        } else if (error != ERROR_SUCCESS && error != ERROR_PIPE_CONNECTED)
            return 5;
        CloseHandle(connect.hEvent);
        ipc::Frame request;
        while (ipc::readFrame(pipe, request, 10000)) {
            ipc::Frame reply{ipc::MessageType::Error,
                             request.requestId,
                             request.contextId,
                             {}};
            if (request.contextId) {
                ipc::writeFrame(pipe, reply, 1000);
                continue;
            }
            switch (request.type) {
            case ipc::MessageType::Hello: {
                reply.type = ipc::MessageType::HelloReply;
                ipc::Writer writer;
                writer.u16(ipc::kVersion);
                reply.payload = writer.take();
                break;
            }
            case ipc::MessageType::GetGlobalSettings:
            case ipc::MessageType::SetGlobalSettings: {
                ipc::GlobalSettingsReply global;
                global.coreActiveLanguage = active;
                if (!i18n::readSettings(file, global.settings))
                    global.error = ipc::SettingsError::SaveFailed;
                else if (request.type == ipc::MessageType::SetGlobalSettings) {
                    ipc::GlobalSettings requested;
                    global.error =
                        ipc::decodeGlobalSettings(request.payload, requested)
                            ? i18n::saveSettings(file, requested,
                                                 global.settings)
                            : ipc::SettingsError::Invalid;
                }
                global.resolvedLanguage =
                    i18n::resolveLanguage(global.settings.language);
                reply.type = ipc::MessageType::GlobalSettingsReply;
                reply.payload = ipc::encodeGlobalSettingsReply(global);
                break;
            }
            case ipc::MessageType::GetInputMethods:
            case ipc::MessageType::SetInputMethods:
                if (request.type == ipc::MessageType::SetInputMethods) {
                    ipc::InputMethodSelection selected;
                    if (!ipc::decodeSelection(request.payload, selected))
                        break;
                    catalog.selection.enabled = selected.enabled;
                    ++catalog.selection.revision;
                }
                reply.type = ipc::MessageType::InputMethodsReply;
                reply.payload = ipc::encodeInputMethods(catalog);
                break;
            case ipc::MessageType::GetMethodConfig:
            case ipc::MessageType::SetMethodConfig: {
                ipc::MethodConfigRequest config;
                if (!ipc::decodeMethodConfigRequest(request.payload, config))
                    break;
                if (request.type == ipc::MessageType::SetMethodConfig) {
                    for (const auto &value : config.values) {
                        if (value.path == "Prediction")
                            prediction = value.value == "True";
                        else if (value.path == "PrevPage")
                            prevPage = value.value;
                    }
                    ++catalog.selection.revision;
                }
                ipc::MethodConfigReply description;
                description.id = config.id;
                description.panel =
                    config.id == "keyboard-us" ? "keyboard" : "pinyin";
                description.revision = catalog.selection.revision;
                if (description.panel == "pinyin") {
                    description.options.push_back(
                        {"Prediction", "Prediction",
                         prediction ? "True" : "False", "False",
                         ipc::ConfigKind::Boolean});
                    description.options.push_back({"PrevPage", "Previous Page",
                                                   prevPage, "Up\nminus",
                                                   ipc::ConfigKind::KeyList});
                }
                reply.type = ipc::MessageType::MethodConfigReply;
                reply.payload = ipc::encodeMethodConfig(description);
                break;
            }
            case ipc::MessageType::GetDictionaries:
                reply.type = ipc::MessageType::DictionariesReply;
                reply.payload = ipc::encodeDictionariesReply(
                    {true, "C:/isolated-test/dictionaries", {}, {}});
                break;
            default:
                break;
            }
            if (!ipc::writeFrame(pipe, reply, 1000))
                break;
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}
