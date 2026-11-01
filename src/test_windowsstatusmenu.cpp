#include "windowsstatusmenu.h"
#include <cassert>
#include <fcitx-utils/environ.h>
#include <fcitx/action.h>
#include <fcitx/inputcontext.h>
#include <fcitx/menu.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>

using namespace fcitx;
using namespace fcitx::win32;
class UnstableAction : public SimpleAction {
  public:
    std::string shortText(InputContext *ic) const override {
        const_cast<UnstableAction *>(this)->update(ic);
        return "unstable";
    }
};
class Context : public InputContext {
  public:
    explicit Context(Instance &i) : InputContext(i.inputContextManager()) {}
    ~Context() override { destroy(); }
    const char *frontend() const override { return "test"; }
    std::string commit;

  protected:
    void commitStringImpl(const std::string &s) override { commit += s; }
    void deleteSurroundingTextImpl(int, unsigned) override {}
    void forwardKeyImpl(const ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};
int main() {
    setEnvironment("SKIP_FCITX_USER_PATH", "1");
    setEnvironment("SKIP_FCITX_SYSTEM_PATH", "1");
    Instance instance(0, nullptr);
    Context context(instance), other(instance);
    WindowsStatusMenu bridge(instance, context), second(instance, other);
    SimpleAction parent, shared, separator, unregistered;
    Menu menu;
    separator.setSeparator(true);
    parent.setMenu(&menu);
    shared.setShortText("shared");
    instance.userInterfaceManager().registerAction("test-shared", &shared);
    shared.connect<SimpleAction::Activated>(
        [](InputContext *ic) { ic->commitString("from action"); });
    menu.addAction(&separator);
    menu.addAction(&shared);
    menu.addAction(&unregistered);
    menu.addAction(&separator);
    context.statusArea().addAction(StatusGroup::InputMethod, &parent);
    context.statusArea().addAction(StatusGroup::AfterInputMethod, &shared);
    other.statusArea().addAction(StatusGroup::InputMethod, &shared);
    auto reply = bridge.query("test", false);
    assert(reply.error == ipc::StatusMenuError::None &&
           reply.nodes.size() == 5);
    assert(reply.nodes[0].kind == ipc::StatusMenuNodeKind::Submenu);
    assert(!reply.nodes[2].enabled);
    assert(bridge.query("test", false).generation == reply.generation);
    context.statusArea().removeAction(&shared);
    context.statusArea().addAction(StatusGroup::AfterInputMethod, &shared);
    assert(
        bridge.activate("test", reply.generation, reply.nodes.back().nodeId) ==
        ipc::StatusMenuError::Stale);
    reply = bridge.query("test", false);
    instance.postEvent(
        InputContextFlushUIEvent(UserInterfaceComponent::StatusArea, &context));
    assert(bridge.query("test", false).generation == reply.generation);
    assert(second.activate("test", reply.generation, reply.nodes[1].nodeId) ==
           ipc::StatusMenuError::Stale);
    assert(bridge.activate("test", reply.generation, reply.nodes[1].nodeId) ==
           ipc::StatusMenuError::None);
    assert(context.commit == "from action");
    assert(bridge.activate("test", reply.generation, reply.nodes[1].nodeId) ==
           ipc::StatusMenuError::Stale);
    reply = bridge.query("test", false);
    shared.update(&other);
    assert(bridge.query("test", false).generation == reply.generation);
    shared.update(&context);
    assert(bridge.activate("test", reply.generation, reply.nodes[1].nodeId) ==
           ipc::StatusMenuError::Stale);
    reply = bridge.query("test", false);
    shared.setShortText("changed without notification");
    assert(bridge.activate("test", reply.generation, reply.nodes[1].nodeId) ==
           ipc::StatusMenuError::Stale);
    auto leaf = std::make_unique<SimpleAction>();
    instance.userInterfaceManager().registerAction("reused-name", leaf.get());
    menu.addAction(leaf.get());
    reply = bridge.query("test", false);
    const auto old = reply.generation;
    leaf.reset();
    leaf = std::make_unique<SimpleAction>();
    instance.userInterfaceManager().registerAction("reused-name", leaf.get());
    menu.addAction(leaf.get());
    assert(bridge.activate("test", old, reply.nodes[1].nodeId) ==
           ipc::StatusMenuError::Stale);
    reply = bridge.query("test", false);
    bridge.release();
    assert(bridge.activate("test", reply.generation, reply.nodes[1].nodeId) ==
           ipc::StatusMenuError::Stale);
    menu.addAction(&parent);
    assert(bridge.query("test", false).error == ipc::StatusMenuError::Failed);
    menu.removeAction(&parent);
    shared.setShortText(std::string(1025, 'x'));
    assert(bridge.query("test", false).error == ipc::StatusMenuError::Failed);
    shared.setShortText("valid");
    reply = bridge.query("test", true);
    assert(reply.error == ipc::StatusMenuError::None);
    assert(bridge.query("test", true).error == ipc::StatusMenuError::Busy);
    SimpleAction alias;
    alias.setMenu(&menu);
    context.statusArea().addAction(StatusGroup::BeforeInputMethod, &alias);
    reply = bridge.query("test", false);
    assert(reply.error == ipc::StatusMenuError::None);
    unsigned occurrences = 0;
    for (const auto &node : reply.nodes)
        if (node.label == "valid")
            ++occurrences;
    assert(occurrences == 3);
    menu.removeAction(&shared);
    menu.addAction(&shared);
    assert(
        bridge.activate("test", reply.generation, reply.nodes.back().nodeId) ==
        ipc::StatusMenuError::Stale);
    alignas(SimpleAction) unsigned char storage[sizeof(SimpleAction)];
    auto *reused = std::construct_at(reinterpret_cast<SimpleAction *>(storage));
    instance.userInterfaceManager().registerAction("same-address", reused);
    menu.addAction(reused);
    const auto oldId = reused->id();
    reply = bridge.query("test", false);
    std::destroy_at(reused);
    reused = std::construct_at(reinterpret_cast<SimpleAction *>(storage));
    instance.userInterfaceManager().registerAction("same-address", reused);
    menu.addAction(reused);
    assert(reused->id() == oldId);
    assert(
        bridge.activate("test", reply.generation, reply.nodes.back().nodeId) ==
        ipc::StatusMenuError::Stale);
    std::destroy_at(reused);
    UnstableAction unstable;
    context.statusArea().addAction(StatusGroup::AfterInputMethod, &unstable);
    assert(bridge.query("test", false).error == ipc::StatusMenuError::Busy);
    context.statusArea().removeAction(&unstable);
    SimpleAction throws;
    instance.userInterfaceManager().registerAction("throwing", &throws);
    throws.connect<SimpleAction::Activated>([](InputContext *) { throw 42; });
    context.statusArea().addAction(StatusGroup::AfterInputMethod, &throws);
    reply = bridge.query("test", false);
    const auto throwing = reply.nodes.back().nodeId;
    assert(bridge.activate("test", reply.generation, throwing) ==
           ipc::StatusMenuError::Failed);
    assert(bridge.activate("test", reply.generation, throwing) ==
           ipc::StatusMenuError::Stale);
}
