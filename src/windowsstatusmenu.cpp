#include "windowsstatusmenu.h"
#include <algorithm>
#include <chrono>
#include <fcitx/action.h>
#include <fcitx/inputcontext.h>
#include <fcitx/menu.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <stdexcept>
#include <unordered_set>

namespace fcitx::win32 {
struct WindowsStatusMenu::Observed {
    uint64_t identity;
    bool alive = true;
    ScopedConnection destroyed, updated;
};
struct WindowsStatusMenu::Tree {
    ipc::StatusMenuReply reply;
    std::vector<uint64_t> identities;
    std::unordered_map<ConnectableObject *, std::shared_ptr<Observed>> objects;
    std::unordered_map<uint32_t, Action *> commands;
};
uint64_t WindowsStatusMenu::nextIdentity() {
    static uint64_t value = 0;
    if (value == UINT64_MAX)
        throw std::overflow_error("status menu identity exhausted");
    return ++value;
}
WindowsStatusMenu::WindowsStatusMenu(Instance &instance, InputContext &context)
    : instance_(instance), context_(context), revision_(nextIdentity()) {
    uiRequestWatcher_ = instance.watchEvent(
        EventType::InputContextUpdateUI, EventWatcherPhase::PostInputMethod,
        [this](Event &event) {
            auto &ui = static_cast<InputContextUpdateUIEvent &>(event);
            if (ui.inputContext() == &context_ &&
                ui.component() == UserInterfaceComponent::StatusArea) {
                pendingUi_ = true;
                invalidate();
            }
        });
    uiWatcher_ = instance.watchEvent(
        EventType::InputContextFlushUI, EventWatcherPhase::PostInputMethod,
        [this](Event &event) {
            auto &ui = static_cast<InputContextFlushUIEvent &>(event);
            if (ui.inputContext() == &context_ &&
                ui.component() == UserInterfaceComponent::StatusArea) {
                // StatusArea mutations must revoke commands immediately;
                // their later batched flush is the same update.
                if (!pendingUi_)
                    invalidate();
                pendingUi_ = false;
            }
        });
}
WindowsStatusMenu::~WindowsStatusMenu() = default;
void WindowsStatusMenu::invalidate() {
    if (!dirty_) {
        try {
            revision_ = nextIdentity();
        } catch (...) {
            revision_ = 0;
        }
        dirty_ = true;
    }
    if (tree_)
        tree_->reply.generation = 0;
}
void WindowsStatusMenu::release() {
    invalidate();
    tree_.reset();
    observed_.clear();
}
uint64_t WindowsStatusMenu::revision(const std::string &method) {
    if (method_ != method) {
        invalidate();
        method_ = method;
    }
    return revision_;
}
std::shared_ptr<WindowsStatusMenu::Observed>
WindowsStatusMenu::observe(ConnectableObject *object, bool menu) {
    auto &entry = observed_[object];
    if (entry && entry->alive)
        return entry;
    entry = std::make_shared<Observed>();
    entry->identity = nextIdentity();
    const auto weak = std::weak_ptr<Observed>(entry);
    entry->destroyed = object->connect<ObjectDestroyed>([this, weak](void *) {
        if (const auto guard = weak.lock())
            guard->alive = false;
        invalidate();
    });
    if (menu)
        entry->updated =
            object->connect<Menu::Update>([this] { invalidate(); });
    else
        entry->updated =
            object->connect<Action::Update>([this](InputContext *ic) {
                if (!ic || ic == &context_)
                    invalidate();
            });
    return entry;
}
WindowsStatusMenu::Tree WindowsStatusMenu::build(const std::string &method) {
    Tree result;
    result.reply.revision = revision(method);
    result.reply.inputMethod = method;
    size_t visited = 0, bytes = 29 + method.size();
    std::unordered_set<Action *> path;
    auto visit = [&](auto &&self, const std::vector<Action *> &actions,
                     uint32_t parent, size_t depth) -> void {
        if (actions.size() > ipc::kMaxStatusMenuNodes)
            throw std::runtime_error("menu quota");
        // Guard all siblings before calling any plugin getter.
        std::vector<std::shared_ptr<Observed>> guards;
        for (auto *a : actions) {
            if (!a)
                throw std::runtime_error("null action");
            auto guard = observe(a, false);
            result.objects[a] = guard;
            guards.push_back(guard);
        }
        bool separator = true;
        for (size_t index = 0; index < actions.size(); ++index) {
            auto *a = actions[index];
            const auto guard = guards[index];
            if (!guard->alive || ++visited > ipc::kMaxStatusMenuNodes ||
                depth > ipc::kMaxStatusMenuDepth || !path.insert(a).second)
                throw std::runtime_error("invalid menu graph");
            ipc::StatusMenuNode n;
            n.nodeId = static_cast<uint32_t>(visited);
            n.parentId = parent;
            result.identities.push_back(guard->identity);
            if (a->isSeparator()) {
                path.erase(a);
                if (separator)
                    continue;
                n.kind = ipc::StatusMenuNodeKind::Separator;
                result.reply.nodes.push_back(n);
                separator = true;
                continue;
            }
            n.label = a->shortText(&context_);
            if (!guard->alive)
                throw std::runtime_error("action destroyed in getter");
            n.checkable = a->isCheckable();
            n.checked = n.checkable && a->isChecked(&context_);
            if (!guard->alive)
                throw std::runtime_error("action destroyed in getter");
            for (const auto &key : a->hotkey()) {
                if (!n.shortcutText.empty())
                    n.shortcutText += ", ";
                n.shortcutText += key.toString();
                if (n.shortcutText.size() > 256)
                    throw std::runtime_error("shortcut quota");
            }
            bytes += 20 + n.label.size() + n.shortcutText.size();
            if (n.label.size() > 1024 || bytes > ipc::kMaxStatusMenuBytes ||
                !ipc::statusMenuUtf8(n.label) ||
                !ipc::statusMenuUtf8(n.shortcutText))
                throw std::runtime_error("invalid menu text");
            if (auto *menu = a->menu()) {
                const auto menuGuard = observe(menu, true);
                result.objects[menu] = menuGuard;
                result.identities.push_back(menuGuard->identity);
                n.kind = ipc::StatusMenuNodeKind::Submenu;
                n.enabled = true;
                const auto position = result.reply.nodes.size();
                result.reply.nodes.push_back(n);
                self(self, menu->actions(), n.nodeId, depth + 1);
                if (result.reply.nodes.size() == position + 1)
                    result.reply.nodes.pop_back();
                else
                    separator = false;
            } else {
                n.enabled = a->id() &&
                            instance_.userInterfaceManager().lookupActionById(
                                a->id()) == a;
                result.reply.nodes.push_back(n);
                if (n.enabled)
                    result.commands.emplace(n.nodeId, a);
                separator = false;
            }
            path.erase(a);
        }
        if (separator && !result.reply.nodes.empty() &&
            result.reply.nodes.back().parentId == parent &&
            result.reply.nodes.back().kind ==
                ipc::StatusMenuNodeKind::Separator)
            result.reply.nodes.pop_back();
    };
    for (auto group : {StatusGroup::BeforeInputMethod, StatusGroup::InputMethod,
                       StatusGroup::AfterInputMethod}) {
        const auto before = result.reply.nodes.size();
        if (before)
            result.reply.nodes.push_back({static_cast<uint32_t>(++visited), 0,
                                          ipc::StatusMenuNodeKind::Separator});
        const auto start = result.reply.nodes.size();
        visit(visit, context_.statusArea().actions(group), 0, 1);
        if (result.reply.nodes.size() == start && before)
            result.reply.nodes.pop_back();
    }
    result.reply.generation = result.reply.nodes.empty() ? 0 : 1;
    if (!ipc::validStatusMenu(result.reply))
        throw std::runtime_error("invalid status menu");
    return result;
}
ipc::StatusMenuReply WindowsStatusMenu::query(const std::string &method,
                                              bool throttle) {
    using Error = ipc::StatusMenuError;
    revision(method);
    auto error = [&](Error code) {
        ipc::StatusMenuReply reply;
        reply.error = code;
        reply.revision = revision_;
        return reply;
    };
    if (!revision_)
        return error(Error::Unavailable);
    const auto now = std::chrono::steady_clock::now();
    if (throttle && now < nextQuery_)
        return error(Error::Busy);
    if (throttle)
        nextQuery_ = now + std::chrono::milliseconds(500);
    try {
        for (int attempt = 0; attempt < 2; ++attempt) {
            dirty_ = false;
            const auto start = revision_;
            auto next = build(method);
            if (dirty_ || start != revision_)
                continue;
            if (tree_ && tree_->reply.generation &&
                tree_->reply.revision == revision_ &&
                tree_->reply.nodes == next.reply.nodes &&
                tree_->identities == next.identities)
                return tree_->reply;
            if (tree_ && (tree_->reply.nodes != next.reply.nodes ||
                          tree_->identities != next.identities)) {
                revision_ = nextIdentity();
                next.reply.revision = revision_;
            }
            next.reply.generation =
                next.reply.nodes.empty() ? 0 : nextIdentity();
            observed_ = next.objects;
            tree_ = std::make_unique<Tree>(std::move(next));
            return tree_->reply;
        }
        release();
        return error(Error::Busy);
    } catch (...) {
        release();
        return error(Error::Failed);
    }
}
ipc::StatusMenuError WindowsStatusMenu::activate(const std::string &method,
                                                 uint64_t generation,
                                                 uint32_t node) {
    using Error = ipc::StatusMenuError;
    if (!tree_ || !generation || tree_->reply.generation != generation ||
        dirty_ || method != method_)
        return Error::Stale;
    const auto current = query(method, false);
    if (current.error != Error::None)
        return current.error;
    if (current.generation != generation)
        return Error::Stale;
    const auto found = tree_->commands.find(node);
    if (found == tree_->commands.end())
        return Error::Invalid;
    auto *action = found->second;
    const auto guard = tree_->objects.at(action);
    if (!guard->alive || !action->id() ||
        instance_.userInterfaceManager().lookupActionById(action->id()) !=
            action)
        return Error::Stale;
    invalidate(); // Single use, including reentrant activation and exceptions.
    try {
        action->activate(&context_);
        return Error::None;
    } catch (...) {
        return Error::Failed;
    }
}
} // namespace fcitx::win32
