#include "tsf.h"

namespace fcitx {
STDMETHODIMP Tsf::OnCompositionTerminated(TfEditCookie ecWrite,
                                          ITfComposition *pComposition) {
    if (pComposition && pComposition == composition_) {
        CComPtr<ITfRange> range;
        if (SUCCEEDED(pComposition->GetRange(&range)) && range) {
            range->SetText(ecWrite, 0, L"", 0);
        }
        composition_.Release();
        ++generation_;
        candidatePositionRequested_ = false;
        edits_.clear();
        editRequested_ = false;
        displayedPreedit_.clear();
        displayedCursor_ = 0;
        hideCandidates();
        PipeClient::KeyReply reply;
        if (remoteContextId_ && pipe_.reset(remoteContextId_, reply)) {
            state_ = std::move(reply);
        } else {
            state_ = {};
        }
    }
    return S_OK;
}
} // namespace fcitx
