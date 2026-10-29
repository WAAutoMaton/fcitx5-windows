#include "../win32/i18n/translator.h"
#include "windowssettings.h"
#include <algorithm>
#include <charconv>
#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/environ.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodmanager.h>
#include <sstream>
#include <stdexcept>

namespace fcitx::win32 {
namespace {
constexpr const char *punctuationOption = "WindowsPunctuation";

i18n::Translator windowsTranslator() {
    const auto language = getEnvironmentOrEmpty("FCITX_LOCALE");
    return i18n::Translator(
        language == "zh_CN"   ? ipc::UiLanguage::SimplifiedChinese
        : language == "zh_TW" ? ipc::UiLanguage::TraditionalChinese
                              : ipc::UiLanguage::English);
}

std::string field(const RawConfig &raw, const std::string &path) {
    const auto *value = raw.valueByPath(path);
    return value ? *value : "";
}
int32_t integer(const RawConfig &raw, const char *path, int32_t fallback) {
    const auto text = field(raw, path);
    int32_t number = fallback;
    std::from_chars(text.data(), text.data() + text.size(), number);
    return number;
}
std::string listValue(const RawConfig &raw) {
    std::string result;
    for (const auto &key : raw.subItems()) {
        if (!result.empty()) {
            result += '\n';
        }
        result += field(raw, key);
    }
    return result;
}
void writeValue(RawConfig &raw, const ipc::ConfigOption &option,
                const std::string &value) {
    if (option.kind != ipc::ConfigKind::KeyList) {
        raw.setValueByPath(option.path, value);
        return;
    }
    auto node = raw.get(option.path, true);
    node->removeAll();
    std::istringstream stream(value);
    std::string key;
    unsigned index = 0;
    while (std::getline(stream, key)) {
        if (!key.empty()) {
            node->setValueByPath(std::to_string(index++), key);
        }
    }
}
void describe(const RawConfig &description, const std::string &type,
              const RawConfig &values, const std::string &prefix,
              std::vector<ipc::ConfigOption> &options, unsigned depth = 0) {
    const auto root = description.get(type);
    if (!root || depth > 8) {
        return;
    }
    for (const auto &key : root->subItems()) {
        const auto node = root->get(key);
        const auto kind = field(*node, "Type");
        const auto path = prefix + key;
        if (description.get(kind)) {
            describe(description, kind, values, path + "/", options, depth + 1);
            continue;
        }
        ipc::ConfigOption option;
        option.path = path;
        option.label = field(*node, "Description");
        option.value = field(values, path);
        option.defaultValue = field(*node, "DefaultValue");
        if (kind == "Boolean") {
            option.kind = ipc::ConfigKind::Boolean;
        } else if (kind == "Integer") {
            option.kind = ipc::ConfigKind::Integer;
            option.minimum = integer(*node, "IntMin", INT32_MIN);
            option.maximum = integer(*node, "IntMax", INT32_MAX);
        } else if (auto enums = node->get("Enum")) {
            option.kind = ipc::ConfigKind::Enum;
            for (const auto &index : enums->subItems()) {
                auto label = field(*node, "EnumI18n/" + index);
                auto value = field(*enums, index);
                option.choices.push_back(
                    {value, label.empty() ? value : label});
            }
        } else if (kind == "List|Key") {
            option.kind = ipc::ConfigKind::KeyList;
            if (auto list = values.get(path)) {
                option.value = listValue(*list);
            }
            if (auto list = node->get("DefaultValue")) {
                option.defaultValue = listValue(*list);
            }
        } else if (kind != "String" && kind != "Key") {
            continue;
        }
        options.push_back(std::move(option));
        if (options.size() >= ipc::kMaxConfigOptions) {
            return;
        }
    }
}
InputMethodEngine *engine(Instance &instance, const InputMethodEntry &entry) {
    return dynamic_cast<InputMethodEngine *>(
        instance.addonManager().addon(entry.addon(), true));
}
} // namespace

const ipc::InputMethodInfo *
WindowsSettings::method(const std::string &id) const {
    auto found = std::find_if(methods_.begin(), methods_.end(),
                              [&](const auto &item) { return item.id == id; });
    return found == methods_.end() ? nullptr : &*found;
}
bool WindowsSettings::selected(const std::string &id) const {
    return std::find(enabled_.begin(), enabled_.end(), id) != enabled_.end();
}
std::string WindowsSettings::configKey(const std::string &id) const {
    const auto *entry = instance_.inputMethodManager().entry(id);
    return entry && entry->addon() == "pinyin" ? "pinyin" : id;
}

void WindowsSettings::initializeMethods() {
    const auto translator = windowsTranslator();
    instance_.inputMethodManager().foreachEntries(
        [&](const InputMethodEntry &entry) {
            if (!ipc::validMethodId(entry.uniqueName()) ||
                methods_.size() >= ipc::kMaxInputMethods) {
                return true;
            }
            ipc::InputMethodInfo info;
            info.id = entry.uniqueName();
            info.name = entry.name();
            info.icon = entry.icon();
            info.keyboard =
                entry.isKeyboard() || entry.addon() == "windowskeyboard";
            info.panel = info.keyboard ? "keyboard" : entry.addon();
            info.iconLabel = entry.label();
            if (info.id == "keyboard-us") {
                info.name = translator.text("input-method", "English Keyboard");
            }
            if (info.id == "pinyin") {
                info.name = translator.text("input-method", "Pinyin");
                info.iconLabel = "\xe6\x8b\xbc";
            }
            if (info.id == "shuangpin") {
                info.name = translator.text("input-method", "Shuangpin");
                info.iconLabel = "\xe5\x8f\x8c";
            }
            if (info.id == "wbx") {
                info.name = translator.text("input-method", "Wubi");
            }
            if (info.id == "wbpy") {
                info.name = translator.text("input-method", "Wubi Pinyin");
                info.iconLabel = "W";
                info.icon = "fcitx-wbpy";
            }
            if (info.iconLabel.empty()) {
                info.iconLabel = info.keyboard ? "A" : "F";
            }
            // Entry metadata lets future addons choose a shared panel and
            // compact icon.
            auto fd = StandardPaths::global().open(
                StandardPathsType::PkgData, "inputmethod/" + info.id + ".conf");
            if (fd.isValid()) {
                RawConfig metadata;
                readFromIni(metadata, fd.fd());
                auto panel =
                    field(metadata, "InputMethod/WindowsSettingsPanel");
                auto label = field(metadata, "InputMethod/WindowsIconLabel");
                if (!panel.empty()) {
                    info.panel = panel;
                }
                if (!label.empty()) {
                    info.iconLabel = label;
                }
            }
            if (ipc::validMethodId(info.icon)) {
                auto icon = StandardPaths::global().locate(
                    StandardPathsType::Data,
                    "icons/hicolor/48x48/apps/org.fcitx.Fcitx5." + info.icon +
                        ".png");
                if (icon.empty()) {
                    icon = StandardPaths::global().locate(
                        StandardPathsType::Data,
                        "icons/hicolor/48x48/apps/" + info.icon + ".png");
                }
                if (!icon.empty()) {
                    info.icon = icon.generic_string();
                }
            }
            try {
                auto *addon = engine(instance_, entry);
                info.available = addon != nullptr;
                if (info.available && entry.addon() == "table") {
                    info.available =
                        addon->getConfigForInputMethod(entry) != nullptr;
                }
                if (info.available) {
                    if (auto config = overrides_.get(configKey(info.id))) {
                        setMethodValues(entry, *config);
                    }
                }
            } catch (const std::exception &error) {
                FCITX_WARN() << "Input method " << info.id
                             << " unavailable: " << error.what();
                info.available = false;
            }
            methods_.push_back(std::move(info));
            return true;
        });
    std::sort(methods_.begin(), methods_.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    if (enabled_.empty()) {
        enabled_.push_back("keyboard-us");
        if (const auto *pinyin = method("pinyin");
            pinyin && pinyin->available) {
            enabled_.push_back(settings_.scheme == ipc::PinyinScheme::Double
                                   ? "shuangpin"
                                   : "pinyin");
        }
    }
    for (const auto &id : enabled_) {
        if (!method(id)) {
            methods_.push_back({id, id, "", "?", "", false, false});
        }
    }
    if (std::none_of(enabled_.begin(), enabled_.end(),
                     [&](const auto &id) {
                         const auto *info = method(id);
                         return info && info->available;
                     }) &&
        !selected("keyboard-us")) {
        enabled_.insert(enabled_.begin(), "keyboard-us");
    }
    // Fcitx reserves the first group entry for the inactive keyboard. The user
    // selection is separate, so removing the keyboard does not alter this ABI.
    InputMethodGroup group("Windows");
    group.setDefaultLayout("us");
    group.inputMethodList().emplace_back("keyboard-us");
    for (const auto &info : methods_) {
        if (info.available && info.id != "keyboard-us") {
            group.inputMethodList().emplace_back(info.id);
        }
    }
    group.setDefaultInputMethod(chineseEntry());
    auto &manager = instance_.inputMethodManager();
    manager.addEmptyGroup(group.name());
    manager.setGroup(std::move(group));
    manager.setCurrentGroup("Windows");
}

ipc::InputMethodsReply
WindowsSettings::inputMethods(ipc::SettingsError error) const {
    return {error, {settings_.revision, enabled_}, methods_};
}
ipc::SettingsError
WindowsSettings::applySelection(const ipc::InputMethodSelection &selection) {
    resetRequired_ = false;
    if (selection.enabled.empty()) {
        return ipc::SettingsError::LastInputMethod;
    }
    if (!ipc::validSelection(selection)) {
        return ipc::SettingsError::Invalid;
    }
    bool available = false;
    for (const auto &id : selection.enabled) {
        const auto *info = method(id);
        if (!info || (!info->available && !selected(id))) {
            return ipc::SettingsError::Unavailable;
        }
        available = available || info->available;
    }
    if (!available) {
        return ipc::SettingsError::LastInputMethod;
    }
    if (selection.revision != settings_.revision ||
        settings_.revision == UINT64_MAX) {
        return ipc::SettingsError::Conflict;
    }
    if (selection.enabled == enabled_) {
        return ipc::SettingsError::None;
    }
    const auto previous = enabled_;
    enabled_ = selection.enabled;
    auto next = settings_;
    ++next.revision;
    if (!save(next)) {
        enabled_ = previous;
        return ipc::SettingsError::SaveFailed;
    }
    settings_ = next;
    resetRequired_ = true;
    return ipc::SettingsError::None;
}

RawConfig WindowsSettings::methodValues(const InputMethodEntry &entry) const {
    RawConfig values;
    auto *addon = engine(instance_, entry);
    if (const auto *config = addon->getConfigForInputMethod(entry)) {
        config->save(values);
    }
    if (entry.addon() == "pinyin") {
        auto *punctuation = instance_.addonManager().addon("punctuation", true);
        if (!punctuation || !punctuation->getConfig()) {
            throw std::runtime_error("Punctuation configuration unavailable");
        }
        RawConfig config;
        punctuation->getConfig()->save(config);
        values.setValueByPath(punctuationOption,
                              field(config, "Enabled") == "True" ? "Chinese"
                                                                 : "English");
    }
    return values;
}

void WindowsSettings::setMethodValues(const InputMethodEntry &entry,
                                      const RawConfig &values) {
    RawConfig engineValues = values;
    auto *addon = engine(instance_, entry);
    AddonInstance *punctuation = nullptr;
    RawConfig punctuationValues;
    if (entry.addon() == "pinyin") {
        engineValues.remove(punctuationOption);
        if (values.get(punctuationOption)) {
            const auto mode = field(values, punctuationOption);
            if (mode != "Chinese" && mode != "English") {
                throw std::runtime_error("Invalid punctuation mode");
            }
            punctuation = instance_.addonManager().addon("punctuation", true);
            if (!punctuation || !punctuation->getConfig()) {
                throw std::runtime_error(
                    "Punctuation configuration unavailable");
            }
            punctuation->getConfig()->save(punctuationValues);
            punctuationValues.setValueByPath(
                "Enabled", mode == "Chinese" ? "True" : "False");
        }
    }
    RawConfig current;
    if (const auto *config = addon->getConfigForInputMethod(entry)) {
        config->save(current);
    }
    if (engineValues != current) {
        addon->setConfigForInputMethod(entry, engineValues);
    }
    if (punctuation) {
        // Keep unrelated punctuation options intact. windows.conf remains the
        // authority; the upstream file is a compatibility mirror.
        punctuation->setConfig(punctuationValues);
    }
}

ipc::MethodConfigReply
WindowsSettings::methodConfig(const std::string &id,
                              ipc::SettingsError error) const {
    ipc::MethodConfigReply reply;
    reply.id = id;
    reply.revision = settings_.revision;
    reply.error = error;
    const auto *info = method(id);
    const auto *entry = instance_.inputMethodManager().entry(id);
    if (!info || !info->available || !entry) {
        reply.error = ipc::SettingsError::Unavailable;
        return reply;
    }
    reply.panel = info->panel;
    try {
        auto *addon = engine(instance_, *entry);
        const auto *config =
            addon ? addon->getConfigForInputMethod(*entry) : nullptr;
        if (config) {
            RawConfig description, values;
            config->dumpDescription(description);
            values = methodValues(*entry);
            describe(description, config->typeName(), values, "",
                     reply.options);
            if (entry->addon() == "pinyin") {
                const auto translator = windowsTranslator();
                ipc::ConfigOption option;
                option.path = punctuationOption;
                option.label = translator.text("input-method", "Punctuation");
                option.value = field(values, punctuationOption);
                option.defaultValue = "Chinese";
                option.kind = ipc::ConfigKind::Enum;
                option.choices = {
                    {"Chinese",
                     translator.text("input-method", "Chinese punctuation")},
                    {"English",
                     translator.text("input-method", "English punctuation")}};
                reply.options.insert(reply.options.begin(), std::move(option));
            }
        }
    } catch (const std::exception &) {
        reply.error = ipc::SettingsError::Unavailable;
    }
    return reply;
}

ipc::SettingsError
WindowsSettings::applyMethodConfig(const ipc::MethodConfigRequest &request) {
    resetRequired_ = false;
    if (request.revision != settings_.revision ||
        settings_.revision == UINT64_MAX) {
        return ipc::SettingsError::Conflict;
    }
    const auto schema = methodConfig(request.id);
    if (schema.error != ipc::SettingsError::None) {
        return schema.error;
    }
    const auto *entry = instance_.inputMethodManager().entry(request.id);
    auto *addon = engine(instance_, *entry);
    const auto *config = addon->getConfigForInputMethod(*entry);
    if (!config) {
        return request.values.empty() ? ipc::SettingsError::None
                                      : ipc::SettingsError::Invalid;
    }
    RawConfig previous, next;
    previous = methodValues(*entry);
    next = previous;
    for (const auto &value : request.values) {
        const auto option = std::find_if(
            schema.options.begin(), schema.options.end(),
            [&](const auto &item) { return item.path == value.path; });
        if (option == schema.options.end() ||
            !ipc::validConfigValue(*option, value.value)) {
            return ipc::SettingsError::Invalid;
        }
        if (option->kind == ipc::ConfigKind::KeyList) {
            std::istringstream keys(value.value);
            std::string key;
            while (std::getline(keys, key)) {
                if (!key.empty() && !Key(key).isValid()) {
                    return ipc::SettingsError::Invalid;
                }
            }
        }
        if (value.path == "ShuangpinProfile" && value.value == "Custom" &&
            option->value != "Custom") {
            return ipc::SettingsError::Invalid;
        }
        writeValue(next, *option, value.value);
    }
    if (previous == next) {
        return ipc::SettingsError::None;
    }
    auto nextSettings = settings_;
    ++nextSettings.revision;
    if (entry->addon() == "pinyin") {
        for (unsigned i = 0;
             i <= static_cast<unsigned>(ipc::ShuangpinProfile::Custom); ++i) {
            if (ipc::profileConfigValue(static_cast<ipc::ShuangpinProfile>(
                    i)) == field(next, "ShuangpinProfile")) {
                nextSettings.profile = static_cast<ipc::ShuangpinProfile>(i);
            }
        }
    }
    const auto oldOverrides = overrides_;
    const auto key = configKey(request.id);
    overrides_[key] = next;
    if (!save(nextSettings)) {
        overrides_ = oldOverrides;
        return ipc::SettingsError::SaveFailed;
    }
    resetRequired_ = true;
    try {
        setMethodValues(*entry, next);
        const auto applied = methodConfig(request.id);
        if (applied.error != ipc::SettingsError::None) {
            throw std::runtime_error("Config unavailable after applying");
        }
        for (const auto &value : request.values) {
            auto option = std::find_if(
                applied.options.begin(), applied.options.end(),
                [&](const auto &item) { return item.path == value.path; });
            if (option == applied.options.end() ||
                option->value != value.value) {
                throw std::runtime_error("Engine rejected configuration");
            }
        }
    } catch (const std::exception &error) {
        FCITX_ERROR() << "Input method configuration rejected: "
                      << error.what();
        overrides_ = oldOverrides;
        try {
            setMethodValues(*entry, previous);
        } catch (...) {
        }
        if (!save(settings_)) {
            FCITX_ERROR() << "Cannot persist configuration rollback";
        }
        return ipc::SettingsError::ApplyFailed;
    }
    settings_ = nextSettings;
    return ipc::SettingsError::None;
}

bool WindowsSettings::select(InputContext *context, const std::string &id) {
    const auto *info = method(id);
    if (!info || !info->available || !selected(id)) {
        return false;
    }
    context->reset();
    instance_.setCurrentInputMethod(context, id, true);
    const auto *entry = instance_.inputMethodEntry(context);
    return entry && entry->uniqueName() == id;
}
bool WindowsSettings::selectIndex(InputContext *context, size_t index) {
    return index < enabled_.size() && select(context, enabled_[index]);
}

bool WindowsSettings::cycle(InputContext *context) {
    const auto *entry = instance_.inputMethodEntry(context);
    auto current =
        entry ? std::find(enabled_.begin(), enabled_.end(), entry->uniqueName())
              : enabled_.end();
    const auto start = current == enabled_.end()
                           ? enabled_.size() - 1
                           : static_cast<size_t>(current - enabled_.begin());
    for (size_t offset = 1; offset <= enabled_.size(); ++offset) {
        const auto &id = enabled_[(start + offset) % enabled_.size()];
        const auto *info = method(id);
        if (info && info->available) {
            if (entry && entry->uniqueName() == id) {
                return true;
            }
            return select(context, id);
        }
    }
    return false;
}
bool WindowsSettings::setMode(InputContext *context, bool enabled) {
    if (chineseMode(context) == enabled) {
        return true;
    }
    for (const auto &id : enabled_) {
        const auto *info = method(id);
        if (info && info->available && info->keyboard != enabled) {
            return select(context, id);
        }
    }
    return true;
}
void WindowsSettings::reconcile(InputContext *context) {
    const auto *entry = instance_.inputMethodEntry(context);
    if (!entry || !selected(entry->uniqueName())) {
        select(context, chineseEntry());
    }
}
void WindowsSettings::decorate(InputContext *context,
                               ipc::KeyReply &reply) const {
    const auto *entry = instance_.inputMethodEntry(context);
    if (!entry) {
        return;
    }
    reply.inputMethod = entry->uniqueName();
    if (const auto *info = method(reply.inputMethod)) {
        reply.inputMethodName = info->name;
        reply.iconLabel = info->iconLabel;
        reply.inputMethodIcon = info->icon;
        reply.inputMethodPanel = info->panel;
    }
}
} // namespace fcitx::win32
