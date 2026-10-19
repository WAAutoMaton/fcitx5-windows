#include "../ipc/installation.h"
#include "keypolicy.h"
#include "tsf.h"

namespace fcitx {
namespace {
constexpr GUID kModeSwitchPreservedKey = {
    0x4d9f2d1a,
    0x6d73,
    0x4a6d,
    {0x9d, 0x4f, 0x8a, 0x61, 0x72, 0x5e, 0x31, 0x09}};
constexpr wchar_t kModeSwitchDescription[] = L"Fcitx5 input method shortcut";
constexpr uint32_t kModifierCaps = 1U << 4;
constexpr uint32_t kModifierRepeat = 1U << 5;

bool keyDown(int key) { return (GetKeyState(key) & 0x8000) != 0; }

uint32_t currentModifiers(LPARAM lParam) {
    uint32_t result = 0;
    if (keyDown(VK_SHIFT)) {
        result |= kModifierShift;
    }
    if (keyDown(VK_CONTROL) || keyDown(VK_LCONTROL) || keyDown(VK_RCONTROL)) {
        result |= kModifierControl;
    }
    if (keyDown(VK_MENU) || keyDown(VK_LMENU) || keyDown(VK_RMENU)) {
        result |= kModifierAlt;
    }
    if (keyDown(VK_LWIN) || keyDown(VK_RWIN)) {
        result |= kModifierSuper;
    }
    if (GetKeyState(VK_CAPITAL) & 1) {
        result |= kModifierCaps;
    }
    if ((lParam & (1LL << 30)) != 0) {
        result |= kModifierRepeat;
    }
    return result;
}

uint32_t scanCode(LPARAM lParam) {
    auto value = static_cast<uint32_t>((lParam >> 16) & 0xff);
    if (lParam & (1LL << 24)) {
        value |= 0x100;
    }
    return value;
}

uint32_t unicodeForKey(WPARAM key, LPARAM details) {
    BYTE keyboard[256]{};
    WCHAR text[4]{};
    if (!GetKeyboardState(keyboard) || (keyboard[VK_CONTROL] & 0x80) ||
        (keyboard[VK_MENU] & 0x80)) {
        return 0;
    }
    const auto count =
        ToUnicodeEx(static_cast<UINT>(key), scanCode(details) & 0xff, keyboard,
                    text, 4, 4, GetKeyboardLayout(0));
    if (count == 1 && text[0] >= 0x20 && text[0] != 0x7f &&
        !(text[0] >= 0xd800 && text[0] <= 0xdfff)) {
        return text[0];
    }
    if (count == 2 && text[0] >= 0xd800 && text[0] <= 0xdbff &&
        text[1] >= 0xdc00 && text[1] <= 0xdfff) {
        return 0x10000 + ((text[0] - 0xd800) << 10) + text[1] - 0xdc00;
    }
    return 0;
}

} // namespace

HRESULT Tsf::initKeyEventSink() {
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    auto result = threadMgr_->QueryInterface(&keystrokeMgr);
    if (FAILED(result)) {
        return result;
    }
    result = keystrokeMgr->AdviseKeyEventSink(clientId_,
                                              (ITfKeyEventSink *)this, TRUE);
    keyEventSinkAdvised_ = SUCCEEDED(result);
    if (keyEventSinkAdvised_) {
        refreshShortcuts();
        if (preservedShortcuts_.empty())
            updatePreservedShortcuts();
    }
    return result;
}

void Tsf::uninitKeyEventSink() {
    if (!keyEventSinkAdvised_) {
        return;
    }
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (threadMgr_->QueryInterface(&keystrokeMgr) != S_OK) {
        return;
    }
    for (const auto &shortcut : preservedShortcuts_)
        keystrokeMgr->UnpreserveKey(shortcut.guid, &shortcut.key);
    preservedShortcuts_.clear();
    keystrokeMgr->UnadviseKeyEventSink(clientId_);
    keyEventSinkAdvised_ = false;
}

void Tsf::refreshShortcuts() {
    if (!pipe_.connected() || GetTickCount64() < nextShortcutCheck_)
        return;
    nextShortcutCheck_ = GetTickCount64() + 1000;
    ipc::GlobalSettingsReply reply;
    if (!pipe_.getGlobalSettings(reply) ||
        reply.error != ipc::SettingsError::None)
        return;
    if (reply.settings.shortcuts != shortcuts_ || preservedShortcuts_.empty()) {
        shortcuts_ = std::move(reply.settings.shortcuts);
        updatePreservedShortcuts();
    }
}

void Tsf::updatePreservedShortcuts() {
    if (!keyEventSinkAdvised_)
        return;
    CComPtr<ITfKeystrokeMgr> manager;
    if (FAILED(threadMgr_->QueryInterface(&manager)))
        return;
    for (const auto &shortcut : preservedShortcuts_)
        manager->UnpreserveKey(shortcut.guid, &shortcut.key);
    preservedShortcuts_.clear();
    for (const auto &list : shortcuts_) {
        for (const auto &line : ipc::shortcutLines(list)) {
            ipc::ShortcutKey parsed;
            if (!ipc::parseShortcut(line, parsed))
                continue;
            const auto scan = parsed.scan;
            parsed.key = ipc::shortcutVirtualKey(parsed);
            if (!parsed.key)
                continue;
            UINT modifiers = 0;
            if (parsed.modifiers & kModifierControl)
                modifiers |= TF_MOD_CONTROL;
            if (parsed.modifiers & kModifierShift)
                modifiers |= TF_MOD_SHIFT;
            if (parsed.modifiers & kModifierAlt)
                modifiers |= TF_MOD_ALT;
            if (parsed.modifiers & kModifierSuper)
                continue; // TSF preserved keys have no Windows-key modifier.
            auto guid = kModeSwitchPreservedKey;
            guid.Data1 += preservedShortcutSequence_++;
            TF_PRESERVEDKEY key{parsed.key, modifiers};
            if (SUCCEEDED(manager->PreserveKey(
                    clientId_, guid, &key, kModeSwitchDescription,
                    static_cast<ULONG>(std::size(kModeSwitchDescription) - 1))))
                preservedShortcuts_.push_back(
                    {guid, key, scan, parsed.modifiers});
        }
    }
}

bool Tsf::pressedCycleShortcut(UINT &key, UINT &scan,
                               unsigned &modifiers) const {
    ipc::ShortcutKey shortcut;
    if (!ipc::pressedShortcut(shortcuts_[0], currentModifiers(0), keyDown,
                              shortcut))
        return false;
    key = ipc::shortcutVirtualKey(shortcut);
    scan = shortcut.scan;
    modifiers = shortcut.modifiers;
    return true;
}

BOOL Tsf::processKey(ITfContext *context, WPARAM wParam, LPARAM lParam,
                     bool release, uint32_t modifiers) {
    if (!inputAllowed_ || context != textEditSinkContext_)
        return FALSE;
    if (!refreshContextPolicy()) {
        clearRemoteContext();
        return FALSE;
    }
    if (!win32::ipc::clientInstallationAllowsUse() || context == nullptr ||
        context != textEditSinkContext_ || remoteContextId_ == 0 ||
        !pipe_.connected() || !foreground_) {
        return FALSE;
    }
    PipeClient::KeyReply reply;
    if (modifiers == UINT32_MAX) {
        modifiers = currentModifiers(lParam);
    }
    const auto action = ipc::shortcutAction(
        shortcuts_, static_cast<UINT>(wParam), scanCode(lParam), modifiers);
    if (!release && action == 0) {
        if ((modifiers & kModifierRepeat) ||
            (modeSwitchKeyDown_ && wParam == modeSwitchVirtualKey_ &&
             !modeSwitchPending_))
            return handledKeys_.test(wParam) ? TRUE : FALSE;
        modeSwitchPending_ = false;
    }
    if (release && wParam == modeSwitchVirtualKey_)
        modeSwitchKeyDown_ = false;
    if (!release && action < 0 &&
        !routesKey(static_cast<uint32_t>(wParam), modifiers, keyboardOpen_,
                   !state_.preedit.empty() || !state_.candidates.empty(),
                   state_.inputMethodPanel != "pinyin" &&
                       !state_.inputMethodPanel.empty()))
        return FALSE;
    if (state_.enabled != keyboardOpen_ && !applyKeyboardMode(true)) {
        return FALSE;
    }
    if (!pipe_.key(remoteContextId_, release, static_cast<uint32_t>(wParam),
                   scanCode(lParam), modifiers,
                   static_cast<uint32_t>(GetMessageTime()), reply,
                   unicodeForKey(wParam, lParam))) {
        clearRemoteContext();
        pipe_.disconnect();
        return FALSE;
    }
    bool consumed = reply.consumed;
    if (!release && action == 0 && consumed) {
        modeSwitchKeyDown_ = true;
        modeSwitchVirtualKey_ = static_cast<UINT>(wParam);
        modeSwitchScan_ = scanCode(lParam);
        modeSwitchModifiers_ = modifiers & 15;
    }
    if (win32::diagnostic::tsfLogger().enabled())
        win32::diagnostic::tsfLogger().event(
            win32::diagnostic::Level::Debug, "key_result",
            {{"release", release ? "true" : "false"},
             {"scan_code", scanCode(lParam)},
             {"modifiers", modifiers},
             {"consumed", consumed ? "true" : "false"}});
    if (reply.enabled != keyboardOpen_) {
        setKeyboardOpen(reply.enabled);
    }
    if (wParam < handledKeys_.size()) {
        if (release) {
            consumed = consumed || handledKeys_.test(wParam);
            handledKeys_.reset(wParam);
        } else if (consumed) {
            handledKeys_.set(wParam);
        }
    }
    submitSnapshot(std::move(reply), true);
    return consumed ? TRUE : FALSE;
}

STDMETHODIMP Tsf::OnSetFocus(BOOL foreground) {
    win32::diagnostic::tsfLogger().event(
        win32::diagnostic::Level::Debug, "foreground",
        {{"active", foreground ? "true" : "false"}});
    foreground_ = foreground != FALSE;
    if (!foreground_) {
        about_.close();
        aboutUi_.reset();
        clearRemoteContext();
    } else {
        initRemoteContext();
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyDown(ITfContext *context, WPARAM key, LPARAM details,
                                BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = contextAllowsInput(context) && context == textEditSinkContext_ &&
               foreground_ && remoteContextId_ && pipe_.connected() &&
               (ipc::shortcutAction(shortcuts_, static_cast<UINT>(key),
                                    scanCode(details),
                                    currentModifiers(details)) >= 0 ||
                routesKey(static_cast<uint32_t>(key), currentModifiers(details),
                          keyboardOpen_,
                          !state_.preedit.empty() || !state_.candidates.empty(),
                          state_.inputMethodPanel != "pinyin" &&
                              !state_.inputMethodPanel.empty()));
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                            BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = processKey(pContext, wParam, lParam, false);
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyUp(ITfContext *context, WPARAM key, LPARAM,
                              BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = contextAllowsInput(context) && context == textEditSinkContext_ &&
               foreground_ && remoteContextId_ && pipe_.connected() &&
               key < handledKeys_.size() && handledKeys_.test(key);
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                          BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = processKey(pContext, wParam, lParam, true);
    return S_OK;
}

STDMETHODIMP Tsf::OnPreservedKey(ITfContext *context, REFGUID guid,
                                 BOOL *pfEaten) {
    if (pfEaten == nullptr) {
        return E_INVALIDARG;
    }
    *pfEaten = FALSE;
    for (const auto &shortcut : preservedShortcuts_) {
        if (guid == shortcut.guid) {
            const LPARAM details =
                static_cast<LPARAM>(shortcut.scan & 0xff) << 16 |
                static_cast<LPARAM>((shortcut.scan & 0x100) != 0) << 24;
            *pfEaten = processKey(context, shortcut.key.uVKey, details, false,
                                  shortcut.modifiers);
            break;
        }
    }
    return S_OK;
}
} // namespace fcitx
