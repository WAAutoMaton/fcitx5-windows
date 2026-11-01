#include "../tsf/pipeclient.h"
#include <iostream>
#include <stdexcept>

namespace ipc = fcitx::win32::ipc;
void require(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
ipc::StatusMenuReply menu(fcitx::PipeClient &pipe, uint64_t context,
                          bool populated = false) {
    for (int attempt = 0; attempt < 90; ++attempt) {
        ipc::StatusMenuReply reply;
        require(pipe.getStatusMenu(context, reply), "menu transport");
        if (reply.error == ipc::StatusMenuError::None &&
            (!populated || !reply.nodes.empty()))
            return reply;
        Sleep(550);
    }
    throw std::runtime_error("menu did not become ready");
}
int main(int argc, char **argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--check-ui-test") {
#ifdef FCITX5_UI_TEST
        return 0;
#else
        return 1;
#endif
    }
    try {
        fcitx::PipeClient pipe, peer;
        require(pipe.connect() && peer.connect(), "connect");
        if (argc > 1 && std::string_view(argv[1]) == "--ready")
            return 0;
        const bool rime = argc > 1 && std::string_view(argv[1]) == "--rime";
        ipc::InputMethodsReply methods;
        require(pipe.getInputMethods(methods), "catalog");
        ipc::InputMethodSelection selection{methods.selection.revision,
                                            {"keyboard-us", "pinyin"}};
        if (rime)
            selection.enabled.push_back("rime");
        require(pipe.setInputMethods(selection, methods) &&
                    methods.error == ipc::SettingsError::None,
                "select catalog");
        uint64_t context = 0, other = 0;
        require(pipe.createContext(1ULL << 1, context) && pipe.focusIn(context),
                "focus");
        ipc::KeyReply state;
        require(pipe.selectInputMethod(context, "pinyin", state), "pinyin");
        auto original = menu(pipe, context, true);
        const auto stable = original.generation;
        require(pipe.poll(context, state), "poll");
        Sleep(550);
        require(menu(pipe, context).generation == stable,
                "poll invalidates menu");
        const auto leaf = original.nodes.back();
        require(leaf.kind == ipc::StatusMenuNodeKind::Command && leaf.enabled,
                "Pinyin action missing");
        ipc::StatusMenuActionReply pinyinAction;
        require(pipe.activateStatusMenu(context, original.generation,
                                        leaf.nodeId, pinyinAction) &&
                    pinyinAction.error == ipc::StatusMenuError::None,
                "Pinyin action");
        require(pipe.activateStatusMenu(context, original.generation,
                                        leaf.nodeId, pinyinAction) &&
                    pinyinAction.error == ipc::StatusMenuError::Stale,
                "Pinyin action replay");
        Sleep(550);
        const auto changed = menu(pipe, context, true);
        require(changed.generation != original.generation &&
                    changed.nodes.back().label != leaf.label,
                "Pinyin action state");
        ipc::StatusMenuReply foreign;
        require(!peer.getStatusMenu(context, foreign),
                "foreign connection accepted");
        require(pipe.createContext(1ULL << 1, other) && pipe.focusIn(other),
                "other focus");
        require(pipe.getStatusMenu(context, foreign) &&
                    foreign.error == ipc::StatusMenuError::Unavailable,
                "old focus accepted");
        require(pipe.focusOut(other) && pipe.destroyContext(other) &&
                    pipe.focusIn(context),
                "restore focus");
        if (rime) {
            require(pipe.selectInputMethod(context, "rime", state), "rime");
            // Schema names come from the isolated fixture, not production data.
            const std::string names[] = {
                "\xe6\x9c\x99\xe6\x9c\x88\xe6\x8b\xbc\xe9\x9f\xb3",
                "\xe6\x9c\x99\xe6\x9c\x88\xe6\x8b\xbc\xe9\x9f\xb3\xc2\xb7\xe7"
                "\xae\x80\xe5\x8c\x96\xe5\xad\x97"};
            for (const auto &name : names) {
                ipc::StatusMenuReply view;
                uint32_t node = 0;
                for (int attempt = 0; attempt < 90 && !node; ++attempt) {
                    Sleep(550);
                    view = menu(pipe, context, true);
                    for (const auto &item : view.nodes)
                        if (item.label == name && item.enabled &&
                            item.kind == ipc::StatusMenuNodeKind::Command)
                            node = item.nodeId;
                }
                require(node, "schema missing");
                require(pipe.key(context, false, 'N', 0, 0, 0, state, 'n'),
                        "preedit setup");
                // The input can change status; read a new view before
                // selecting.
                Sleep(550);
                view = menu(pipe, context, true);
                for (const auto &item : view.nodes)
                    if (item.label == name)
                        node = item.nodeId;
                ipc::StatusMenuActionReply action;
                require(pipe.activateStatusMenu(context, view.generation, node,
                                                action) &&
                            action.error == ipc::StatusMenuError::None,
                        "schema action");
                require(action.state.commit.empty() &&
                            action.state.preedit.empty() &&
                            action.state.candidates.empty(),
                        "schema switch committed old input");
                require(pipe.activateStatusMenu(context, view.generation, node,
                                                action) &&
                            action.error == ipc::StatusMenuError::Stale,
                        "replayed action");
                Sleep(550);
                view = menu(pipe, context, true);
                unsigned checked = 0;
                for (const auto &item : view.nodes)
                    if ((item.label == names[0] || item.label == names[1]) &&
                        item.checked) {
                        ++checked;
                        require(item.label == name, "wrong schema check");
                    }
                require(checked == 1, "schema checks");
                for (char key : std::string("hanyu"))
                    require(pipe.key(context, false, key - 'a' + 'A', 0, 0, 0,
                                     state, key),
                            "rime input");
                require(!state.candidates.empty(), "rime candidates");
                require(
                    pipe.key(context, false, VK_SPACE, 0, 0, 0, state, ' ') &&
                        !state.commit.empty(),
                    "rime commit");
                const auto expected = name == names[0]
                                          ? "\xe6\xbc\xa2\xe8\xaa\x9e"
                                          : "\xe6\xb1\x89\xe8\xaf\xad";
                require(state.commit == expected,
                        "Rime simplified/traditional output");
                require(pipe.key(context, false, VK_F4, 0, 0, 0, state) &&
                            state.consumed && !state.candidates.empty(),
                        "F4 schema candidates");
                require(pipe.key(context, false, VK_ESCAPE, 0, 0, 0, state),
                        "close F4");
            }
            require(pipe.key(context, false, VK_OEM_3, 0, 2, 0, state) &&
                        state.consumed && !state.candidates.empty(),
                    "Ctrl+grave schema candidates");
            require(pipe.key(context, false, VK_ESCAPE, 0, 0, 0, state),
                    "close Ctrl+grave");
            Sleep(550);
            auto view = menu(pipe, context, true);
            uint32_t deploy = 0;
            for (const auto &item : view.nodes)
                if (item.label == "Deploy" && item.enabled)
                    deploy = item.nodeId;
            require(deploy, "deploy action missing");
            ipc::StatusMenuActionReply deployed;
            require(pipe.activateStatusMenu(context, view.generation, deploy,
                                            deployed) &&
                        deployed.error == ipc::StatusMenuError::None,
                    "deploy action");
            require(pipe.activateStatusMenu(context, view.generation, deploy,
                                            deployed) &&
                        deployed.error == ipc::StatusMenuError::Stale,
                    "deploy replay");
            bool ready = false;
            for (int attempt = 0; attempt < 90 && !ready; ++attempt) {
                Sleep(550);
                view = menu(pipe, context, true);
                for (const auto &item : view.nodes)
                    if (item.label == names[1] && item.enabled && item.checked)
                        ready = true;
            }
            require(ready, "schema not restored after deployment");
            for (char key : std::string("hanyu"))
                require(pipe.key(context, false, key - 'a' + 'A', 0, 0, 0,
                                 state, key),
                        "post-deploy input");
            require(pipe.key(context, false, VK_SPACE, 0, 0, 0, state, ' ') &&
                        state.commit == "\xe6\xb1\x89\xe8\xaf\xad",
                    "post-deploy simplified commit");
        }
        require(pipe.focusOut(context) && pipe.destroyContext(context),
                "cleanup");
        std::cout
            << "PASS: Pinyin action, menu ownership, focus, stable polling"
            << (rime ? ", two Rime schemas, checks, input/commit and F4" : "")
            << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
