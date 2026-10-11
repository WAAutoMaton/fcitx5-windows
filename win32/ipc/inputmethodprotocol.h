#pragma once

#include "protocol.h"
#include <algorithm>
#include <charconv>
#include <set>

namespace fcitx::win32::ipc {
constexpr uint32_t kMaxInputMethods = 256;
constexpr uint32_t kMaxConfigOptions = 512;

struct InputMethodInfo {
    std::string id, name, icon, iconLabel, panel;
    bool available = false;
    bool keyboard = false;
    bool operator==(const InputMethodInfo &) const = default;
};
struct InputMethodSelection {
    uint64_t revision = 1;
    std::vector<std::string> enabled;
    bool operator==(const InputMethodSelection &) const = default;
};
struct InputMethodsReply {
    SettingsError error = SettingsError::None;
    InputMethodSelection selection;
    std::vector<InputMethodInfo> methods;
};
enum class ConfigKind : uint8_t { Boolean, Integer, Enum, String, KeyList };
struct ConfigChoice {
    std::string value, label;
    bool operator==(const ConfigChoice &) const = default;
};
struct ConfigOption {
    std::string path, label, value, defaultValue;
    ConfigKind kind = ConfigKind::String;
    int32_t minimum = INT32_MIN, maximum = INT32_MAX;
    std::vector<ConfigChoice> choices;
};
struct ConfigValue {
    std::string path, value;
    bool operator==(const ConfigValue &) const = default;
};
struct MethodConfigRequest {
    std::string id;
    uint64_t revision = 0;
    std::vector<ConfigValue> values;
};
struct MethodConfigReply {
    SettingsError error = SettingsError::None;
    std::string id, panel;
    uint64_t revision = 1;
    std::vector<ConfigOption> options;
};

inline bool validMethodId(std::string_view id) {
    return !id.empty() && id.size() <= 128 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}
inline bool validSelection(const InputMethodSelection &selection) {
    if (!selection.revision || selection.enabled.empty() ||
        selection.enabled.size() > kMaxInputMethods) {
        return false;
    }
    std::set<std::string> unique;
    for (const auto &id : selection.enabled) {
        if (!validMethodId(id) || !unique.insert(id).second) {
            return false;
        }
    }
    return true;
}
inline bool validConfigValue(const ConfigOption &option,
                             const std::string &value) {
    if (value.size() > 4096 || value.find('\0') != std::string::npos) {
        return false;
    }
    if (option.kind == ConfigKind::Boolean) {
        return value == "True" || value == "False";
    }
    if (option.kind == ConfigKind::Enum) {
        return std::any_of(
            option.choices.begin(), option.choices.end(),
            [&](const auto &choice) { return choice.value == value; });
    }
    if (option.kind == ConfigKind::Integer) {
        int32_t number = 0;
        auto parsed =
            std::from_chars(value.data(), value.data() + value.size(), number);
        return parsed.ec == std::errc{} &&
               parsed.ptr == value.data() + value.size() &&
               number >= option.minimum && number <= option.maximum;
    }
    return value.find('\r') == std::string::npos &&
           (option.kind == ConfigKind::KeyList ||
            value.find('\n') == std::string::npos);
}
inline void writeSelection(Writer &writer,
                           const InputMethodSelection &selection) {
    writer.u64(selection.revision);
    writer.u32(static_cast<uint32_t>(selection.enabled.size()));
    for (const auto &id : selection.enabled) {
        writer.string(id);
    }
}
inline bool readSelection(Reader &reader, InputMethodSelection &selection,
                          bool allowEmpty = false) {
    uint32_t count = 0;
    if (!reader.u64(selection.revision) || !reader.u32(count) ||
        count > kMaxInputMethods || count > reader.remaining() / 4) {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        std::string id;
        if (!reader.string(id)) {
            return false;
        }
        selection.enabled.push_back(std::move(id));
    }
    return (allowEmpty && selection.enabled.empty() && selection.revision) ||
           validSelection(selection);
}
inline std::vector<uint8_t>
encodeSelection(const InputMethodSelection &selection) {
    Writer writer;
    writeSelection(writer, selection);
    return writer.take();
}
inline bool decodeSelection(const std::vector<uint8_t> &payload,
                            InputMethodSelection &selection) {
    Reader reader(payload.data(), payload.size());
    InputMethodSelection result;
    if (!readSelection(reader, result, true) || reader.remaining()) {
        return false;
    }
    selection = std::move(result);
    return true;
}
inline bool readSettingsError(Reader &reader, SettingsError &error) {
    uint8_t value = 0;
    if (!reader.u8(value) ||
        value > static_cast<uint8_t>(SettingsError::Busy)) {
        return false;
    }
    error = static_cast<SettingsError>(value);
    return true;
}
inline std::vector<uint8_t> encodeInputMethods(const InputMethodsReply &reply) {
    Writer writer;
    writer.u8(static_cast<uint8_t>(reply.error));
    writeSelection(writer, reply.selection);
    writer.u32(static_cast<uint32_t>(reply.methods.size()));
    for (const auto &method : reply.methods) {
        writer.string(method.id);
        writer.string(method.name);
        writer.string(method.icon);
        writer.string(method.iconLabel);
        writer.string(method.panel);
        writer.u8(method.available);
        writer.u8(method.keyboard);
    }
    return writer.take();
}
inline bool decodeInputMethods(const std::vector<uint8_t> &payload,
                               InputMethodsReply &reply) {
    Reader reader(payload.data(), payload.size());
    InputMethodsReply result;
    uint32_t count = 0;
    if (!readSettingsError(reader, result.error) ||
        !readSelection(reader, result.selection) || !reader.u32(count) ||
        count > kMaxInputMethods || count > reader.remaining() / 22) {
        return false;
    }
    std::set<std::string> ids;
    for (uint32_t i = 0; i < count; ++i) {
        InputMethodInfo method;
        uint8_t available = 0, keyboard = 0;
        if (!reader.string(method.id) || !validMethodId(method.id) ||
            !ids.insert(method.id).second || !reader.string(method.name) ||
            !reader.string(method.icon) || !reader.string(method.iconLabel) ||
            !reader.string(method.panel) || !reader.u8(available) ||
            available > 1 || !reader.u8(keyboard) || keyboard > 1) {
            return false;
        }
        method.available = available;
        method.keyboard = keyboard;
        result.methods.push_back(std::move(method));
    }
    if (reader.remaining()) {
        return false;
    }
    reply = std::move(result);
    return true;
}
inline std::vector<uint8_t>
encodeMethodConfigRequest(const MethodConfigRequest &request) {
    Writer writer;
    writer.string(request.id);
    writer.u64(request.revision);
    writer.u32(static_cast<uint32_t>(request.values.size()));
    for (const auto &value : request.values) {
        writer.string(value.path);
        writer.string(value.value);
    }
    return writer.take();
}
inline bool decodeMethodConfigRequest(const std::vector<uint8_t> &payload,
                                      MethodConfigRequest &request) {
    Reader reader(payload.data(), payload.size());
    MethodConfigRequest result;
    uint32_t count = 0;
    if (!reader.string(result.id) || !validMethodId(result.id) ||
        !reader.u64(result.revision) || !reader.u32(count) ||
        count > kMaxConfigOptions || count > reader.remaining() / 8) {
        return false;
    }
    std::set<std::string> paths;
    for (uint32_t i = 0; i < count; ++i) {
        ConfigValue value;
        if (!reader.string(value.path) || value.path.empty() ||
            !paths.insert(value.path).second || !reader.string(value.value)) {
            return false;
        }
        result.values.push_back(std::move(value));
    }
    if (reader.remaining()) {
        return false;
    }
    request = std::move(result);
    return true;
}
inline std::vector<uint8_t> encodeMethodConfig(const MethodConfigReply &reply) {
    Writer writer;
    writer.u8(static_cast<uint8_t>(reply.error));
    writer.string(reply.id);
    writer.string(reply.panel);
    writer.u64(reply.revision);
    writer.u32(static_cast<uint32_t>(reply.options.size()));
    for (const auto &option : reply.options) {
        writer.string(option.path);
        writer.string(option.label);
        writer.string(option.value);
        writer.string(option.defaultValue);
        writer.u8(static_cast<uint8_t>(option.kind));
        writer.u32(static_cast<uint32_t>(option.minimum));
        writer.u32(static_cast<uint32_t>(option.maximum));
        writer.u32(static_cast<uint32_t>(option.choices.size()));
        for (const auto &choice : option.choices) {
            writer.string(choice.value);
            writer.string(choice.label);
        }
    }
    return writer.take();
}
inline bool decodeMethodConfig(const std::vector<uint8_t> &payload,
                               MethodConfigReply &reply) {
    Reader reader(payload.data(), payload.size());
    MethodConfigReply result;
    uint32_t count = 0;
    if (!readSettingsError(reader, result.error) || !reader.string(result.id) ||
        !reader.string(result.panel) || !reader.u64(result.revision) ||
        !result.revision || !reader.u32(count) || count > kMaxConfigOptions ||
        count > reader.remaining() / 29) {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        ConfigOption option;
        uint8_t kind = 0;
        uint32_t minimum = 0, maximum = 0, choices = 0;
        if (!reader.string(option.path) || !reader.string(option.label) ||
            !reader.string(option.value) ||
            !reader.string(option.defaultValue) || !reader.u8(kind) ||
            kind > static_cast<uint8_t>(ConfigKind::KeyList) ||
            !reader.u32(minimum) || !reader.u32(maximum) ||
            !reader.u32(choices) || choices > 256 ||
            choices > reader.remaining() / 8) {
            return false;
        }
        option.kind = static_cast<ConfigKind>(kind);
        option.minimum = static_cast<int32_t>(minimum);
        option.maximum = static_cast<int32_t>(maximum);
        if (option.minimum > option.maximum) {
            return false;
        }
        for (uint32_t j = 0; j < choices; ++j) {
            ConfigChoice choice;
            if (!reader.string(choice.value) || !reader.string(choice.label)) {
                return false;
            }
            option.choices.push_back(std::move(choice));
        }
        result.options.push_back(std::move(option));
    }
    if (reader.remaining()) {
        return false;
    }
    reply = std::move(result);
    return true;
}
} // namespace fcitx::win32::ipc
