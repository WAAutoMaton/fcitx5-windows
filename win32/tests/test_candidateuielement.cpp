#include "../tsf/candidateuielement.h"
#include <cassert>

LONG dllReferences = 0;
void DllAddRef() { ++dllReferences; }
void DllRelease() { --dllReferences; }

int main() {
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    CComPtr<ITfThreadMgr> manager;
    CComPtr<ITfDocumentMgr> document;
    assert(SUCCEEDED(manager.CoCreateInstance(CLSID_TF_ThreadMgr)));
    assert(SUCCEEDED(manager->CreateDocumentMgr(&document)));
    bool requested = true;
    CComPtr<fcitx::CandidateUIElement> element;
    element.Attach(
        new fcitx::CandidateUIElement([&](bool show) { requested = show; }));
    CComPtr<ITfUIElement> base;
    assert(SUCCEEDED(element->QueryInterface(IID_PPV_ARGS(&base))));
    void *unsupported = reinterpret_cast<void *>(1);
    assert(element->QueryInterface(IID_ITfCandidateListUIElementBehavior,
                                   &unsupported) == E_NOINTERFACE);
    assert(!unsupported);
    assert(element->GetString(0, nullptr) == E_INVALIDARG);
    BSTR text = reinterpret_cast<BSTR>(1);
    assert(element->GetString(0, &text) == E_INVALIDARG && !text);
    assert(element->GetCount(nullptr) == E_INVALIDARG);
    assert(element->GetSelection(nullptr) == E_INVALIDARG);
    assert(element->GetCurrentPage(nullptr) == E_INVALIDARG);
    assert(element->GetUpdatedFlags(nullptr) == E_INVALIDARG);
    assert(element->GetDocumentMgr(nullptr) == E_INVALIDARG);
    assert(element->IsShown(nullptr) == E_INVALIDARG);
    UINT selection = 99;
    assert(element->GetSelection(&selection) == S_FALSE && !selection);
    fcitx::win32::ipc::KeyReply reply;
    reply.candidates = {
        {"one", "1", "comment"}, {"two", "2", ""}, {"three", "3", ""}};
    reply.selected = 2;
    const auto flags = element->publish(reply, document);
    assert((flags & (TF_CLUIE_COUNT | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX |
                     TF_CLUIE_SELECTION)) ==
           (TF_CLUIE_COUNT | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX |
            TF_CLUIE_SELECTION));
    UINT count = 0;
    assert(element->GetCount(&count) == S_OK && count == 3);
    assert(element->GetString(0, &text) == S_OK &&
           std::wstring(text) == L"one");
    SysFreeString(text);
    UINT pages[3]{};
    assert(element->GetPageIndex(nullptr, 0, &count) == S_FALSE && count == 1);
    assert(element->GetPageIndex(pages, 3, &count) == S_OK && count == 1 &&
           pages[0] == 0);
    UINT split[] = {0, 2};
    assert(element->SetPageIndex(split, 2) == S_OK);
    UINT page = 99;
    assert(element->GetCurrentPage(&page) == S_OK && page == 1);
    UINT invalid[] = {0, 0};
    assert(element->SetPageIndex(invalid, 2) == E_INVALIDARG);
    invalid[1] = 3;
    assert(element->SetPageIndex(invalid, 2) == E_INVALIDARG);
    ++reply.revision;
    assert(element->publish(reply, document) == 0);
    assert(element->GetPageIndex(pages, 3, &count) == S_OK && count == 2);
    reply.selected = 0;
    assert(element->publish(reply, document) & TF_CLUIE_SELECTION);
    assert(element->GetCurrentPage(&page) == S_OK && page == 0);
    reply.candidates[0].text = "new";
    assert(element->publish(reply, document) & TF_CLUIE_STRING);
    assert(element->GetPageIndex(pages, 3, &count) == S_OK && count == 1);
    assert(element->Show(FALSE) == S_OK && !requested);
    BOOL shown = TRUE;
    assert(element->IsShown(&shown) == S_OK && !shown);
    assert(element->Show(TRUE) == S_OK && requested);
    assert(element->IsShown(&shown) == S_OK && !shown);
    element->setVisible(true);
    assert(element->IsShown(&shown) == S_OK && shown);
    CComPtr<ITfDocumentMgr> retained;
    assert(element->GetDocumentMgr(&retained) == S_OK && retained == document);
    retained.Release();
    element->detach();
    assert(element->Show(TRUE) == E_UNEXPECTED);
    assert(element->GetDocumentMgr(&retained) == E_UNEXPECTED && !retained);
    assert(element->GetCount(&count) == S_OK && !count);
    base.Release();
    element.Release();
    std::vector<std::pair<fcitx::win32::ipc::CandidateAction, UINT>> actions;
    element.Attach(new fcitx::CandidateUIElement(
        [](bool) {},
        [&](fcitx::win32::ipc::CandidateAction action, uint64_t identity,
            UINT index) {
            assert(identity == 42);
            actions.emplace_back(action, index);
            return S_OK;
        }));
    reply.candidateGeneration = 42;
    element->publish(reply, document);
    CComPtr<ITfCandidateListUIElementBehavior> behavior;
    assert(SUCCEEDED(element.QueryInterface(&behavior)));
    assert(behavior->SetSelection(3) == E_INVALIDARG);
    assert(behavior->SetSelection(2) == S_OK);
    assert(behavior->Finalize() == S_OK && actions.back().second == 2);
    assert(behavior->Abort() == S_OK &&
           actions.back().first == fcitx::win32::ipc::CandidateAction::Abort);
    element->detach();
    assert(behavior->Finalize() == E_UNEXPECTED &&
           behavior->Abort() == E_UNEXPECTED);
    behavior.Release();
    element.Release();
    document.Release();
    manager.Release();
    assert(dllReferences == 0);
    CoUninitialize();
}
