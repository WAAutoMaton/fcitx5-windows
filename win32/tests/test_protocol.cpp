#include "../ipc/protocol.h"
#include <cassert>
#include <string>

using namespace fcitx::win32::ipc;

int main() {
    assert(hostCommand(MessageType::OpenUserDataFolder));
    assert(hostCommand(MessageType::RestartServices));
    assert(hostCommand(MessageType::StopServices));
    assert(!hostCommand(MessageType::OpenSettings));
    assert(globalSettingsRequest(MessageType::OpenUserDataFolder));
    assert(globalSettingsRequest(MessageType::RestartServices));
    assert(globalSettingsRequest(MessageType::StopServices));
    assert(globalSettingsRequest(MessageType::GetGlobalSettings));
    assert(globalSettingsRequest(MessageType::SetGlobalSettings));
    GlobalSettings global{UiLanguage::TraditionalChinese, 42};
    GlobalSettings restoredGlobal;
    assert(decodeGlobalSettings(encodeGlobalSettings(global), restoredGlobal) &&
           restoredGlobal == global);
    auto globalPayload = encodeGlobalSettings(global);
    globalPayload[0] = 255;
    assert(!decodeGlobalSettings(globalPayload, restoredGlobal));
    globalPayload = encodeGlobalSettings(global);
    globalPayload.push_back(0);
    assert(!decodeGlobalSettings(globalPayload, restoredGlobal));
    globalPayload = encodeGlobalSettings(global);
    globalPayload.pop_back();
    assert(!decodeGlobalSettings(globalPayload, restoredGlobal));
    GlobalSettingsReply globalReply;
    globalReply.settings = global;
    globalReply.resolvedLanguage = UiLanguage::TraditionalChinese;
    globalReply.coreActiveLanguage = UiLanguage::English;
    GlobalSettingsReply decodedGlobalReply;
    auto globalReplyPayload = encodeGlobalSettingsReply(globalReply);
    assert(decodeGlobalSettingsReply(globalReplyPayload, decodedGlobalReply));
    assert(decodedGlobalReply.settings == global &&
           decodedGlobalReply.coreActiveLanguage == UiLanguage::English);
    globalReplyPayload[10] = static_cast<uint8_t>(UiLanguage::System);
    assert(!decodeGlobalSettingsReply(globalReplyPayload, decodedGlobalReply));
    Frame source;
    source.type = MessageType::KeyDown;
    source.requestId = 42;
    source.contextId = 7;
    Writer writer;
    writer.u32(65);
    writer.string("测试");
    source.payload = writer.take();

    Frame decoded;
    assert(decodeFrame(encodeFrame(source), decoded));
    assert(decoded.type == source.type);
    assert(decoded.requestId == source.requestId);
    assert(decoded.contextId == source.contextId);
    Reader reader(decoded.payload.data(), decoded.payload.size());
    uint32_t key = 0;
    std::string text;
    assert(reader.u32(key));
    assert(reader.string(text));
    assert(key == 65);
    assert(text == "测试");

    auto invalid = encodeFrame(source);
    invalid[0] ^= 0xff;
    assert(!decodeFrame(invalid, decoded));
    invalid = encodeFrame(source);
    invalid[4] = 1;
    assert(!decodeFrame(invalid, decoded));
    KeyReply snapshot;
    snapshot.consumed = true;
    snapshot.preedit = "nihao";
    snapshot.preeditCursor = 5;
    snapshot.revision = 42;
    snapshot.settingsRevision = 7;
    snapshot.candidateGeneration = 11;
    snapshot.selected = 0;
    snapshot.candidates.push_back({"\xe4\xbd\xa0\xe5\xa5\xbd", "1", "ni hao"});
    KeyReply reply;
    auto payload = encodeKeyReply(snapshot);
    assert(decodeKeyReply(payload, reply));
    assert(reply.consumed && reply.preedit == "nihao" &&
           reply.preeditCursor == 5);
    assert(reply.revision == 42 && reply.settingsRevision == 7 &&
           reply.candidateGeneration == 11 &&
           reply.candidates.front().text == snapshot.candidates.front().text);
    payload.push_back(0);
    assert(!decodeKeyReply(payload, reply));
    snapshot.preeditCursor = 6;
    assert(!decodeKeyReply(encodeKeyReply(snapshot), reply));
    snapshot.preeditCursor = 5;
    snapshot.selected = 1;
    assert(!decodeKeyReply(encodeKeyReply(snapshot), reply));
    snapshot.selected = UINT32_MAX;
    snapshot.candidates.resize(kMaxCandidates + 1);
    assert(!decodeKeyReply(encodeKeyReply(snapshot), reply));
    PinyinSettings settings;
    settings.scheme = PinyinScheme::Double;
    settings.profile = ShuangpinProfile::Xiaohe;
    settings.revision = 19;
    PinyinSettings restored;
    assert(decodeSettings(encodeSettings(settings), restored) &&
           restored == settings);
    auto settingsPayload = encodeSettings(settings);
    settingsPayload[0] = 2;
    assert(!decodeSettings(settingsPayload, restored));
    settingsPayload = encodeSettings(settings);
    settingsPayload[1] = 255;
    assert(!decodeSettings(settingsPayload, restored));
    settingsPayload = encodeSettings(settings);
    settingsPayload.pop_back();
    assert(!decodeSettings(settingsPayload, restored));
    settingsPayload = encodeSettings(settings);
    settingsPayload.push_back(0);
    assert(!decodeSettings(settingsPayload, restored));
    settings.revision = 0;
    assert(!decodeSettings(encodeSettings(settings), restored));
    SettingsReply settingsReply;
    settingsReply.error = SettingsError::Conflict;
    settingsReply.pinyinAvailable = settingsReply.shuangpinAvailable = true;
    settingsReply.settings = restored;
    SettingsReply restoredReply;
    assert(
        decodeSettingsReply(encodeSettingsReply(settingsReply), restoredReply));
    assert(restoredReply.error == SettingsError::Conflict &&
           restoredReply.settings == restored &&
           restoredReply.shuangpinAvailable);
    assert(profileConfigValue(ShuangpinProfile::GB) == "GB Standard");
    assert(globalSettingsRequest(MessageType::GetSettings) &&
           !globalSettingsRequest(MessageType::SetMode));
    assert(globalSettingsRequest(MessageType::GetDictionaries));
    DictionariesReply dictionaries;
    dictionaries.available = true;
    dictionaries.directory = "C:/Users/test/pinyin/dictionaries";
    dictionaries.dictionaries = {
        {"poetry.dict", "C:/poetry.dict", DictionaryStatus::Loaded},
        {"broken.dict", "C:/broken.dict", DictionaryStatus::Failed},
        {"pending.dict", "C:/pending.dict", DictionaryStatus::Loading},
        {"disabled.dict", "C:/disabled.dict", DictionaryStatus::Disabled}};
    DictionariesReply restoredDictionaries;
    const auto dictionaryPayload = encodeDictionariesReply(dictionaries);
    assert(decodeDictionariesReply(dictionaryPayload, restoredDictionaries) &&
           restoredDictionaries == dictionaries);
    for (size_t length = 0; length < dictionaryPayload.size(); ++length) {
        const auto saved = restoredDictionaries;
        assert(!decodeDictionariesReply(
            {dictionaryPayload.begin(), dictionaryPayload.begin() + length},
            restoredDictionaries));
        assert(restoredDictionaries == saved);
    }
    auto badDictionary = dictionaryPayload;
    badDictionary.push_back(0);
    assert(!decodeDictionariesReply(badDictionary, restoredDictionaries));
    badDictionary = dictionaryPayload;
    badDictionary[0] = 2;
    assert(!decodeDictionariesReply(badDictionary, restoredDictionaries));
    badDictionary = dictionaryPayload;
    badDictionary.back() = 4;
    assert(!decodeDictionariesReply(badDictionary, restoredDictionaries));
    dictionaries.dictionaries.resize(kMaxDictionaries + 1);
    assert(!decodeDictionariesReply(encodeDictionariesReply(dictionaries),
                                    restoredDictionaries));
    InputMethodsReply methods;
    methods.selection = {8, {"keyboard-us", "pinyin", "wbx"}};
    methods.methods = {
        {"pinyin", "Pinyin", "fcitx-pinyin", "P", "pinyin", true, false}};
    InputMethodsReply decodedMethods;
    const auto methodsPayload = encodeInputMethods(methods);
    assert(decodeInputMethods(methodsPayload, decodedMethods));
    assert(decodedMethods.selection == methods.selection &&
           decodedMethods.methods == methods.methods);
    for (size_t n = 0; n < methodsPayload.size(); ++n) {
        assert(!decodeInputMethods(
            {methodsPayload.begin(), methodsPayload.begin() + n},
            decodedMethods));
    }
    assert(!validSelection({1, {"pinyin", "pinyin"}}));
    assert(!validSelection({1, {"../pinyin"}}));
    assert(!validSelection({0, {"pinyin"}}));
    InputMethodSelection selection;
    assert(decodeSelection(encodeSelection({1, {}}), selection) &&
           selection.enabled.empty());
    MethodConfigReply config;
    config.id = "pinyin";
    config.panel = "pinyin";
    config.revision = 9;
    ConfigOption option;
    option.path = "Fuzzy/IN_ING";
    option.label = "in <-> ing";
    option.kind = ConfigKind::Boolean;
    option.value = "True";
    option.defaultValue = "False";
    config.options.push_back(option);
    assert(validConfigValue(option, "False") &&
           !validConfigValue(option, "true"));
    option.kind = ConfigKind::Integer;
    option.minimum = 3;
    option.maximum = 10;
    assert(validConfigValue(option, "7") && !validConfigValue(option, "11") &&
           !validConfigValue(option, "7.5"));
    MethodConfigReply decodedConfig;
    const auto configPayload = encodeMethodConfig(config);
    assert(decodeMethodConfig(configPayload, decodedConfig) &&
           decodedConfig.options.front().path == "Fuzzy/IN_ING");
    for (size_t n = 0; n < configPayload.size(); ++n) {
        assert(!decodeMethodConfig(
            {configPayload.begin(), configPayload.begin() + n}, decodedConfig));
    }
    MethodConfigRequest request{"pinyin", 9, {{"Fuzzy/IN_ING", "True"}}},
        restoredRequest;
    assert(decodeMethodConfigRequest(encodeMethodConfigRequest(request),
                                     restoredRequest));
    assert(restoredRequest.values == request.values);
    request.values.push_back(request.values.front());
    assert(!decodeMethodConfigRequest(encodeMethodConfigRequest(request),
                                      restoredRequest));
    return 0;
}
