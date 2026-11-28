#include "engine.hpp"
#include "window_math.hpp"
#include <algorithm>
#include <chrono>
#include <map>

namespace xfc {

namespace {
using Clock = std::chrono::steady_clock;
}

class TrialSim : public ISim {
public:
    TrialSim(IGame* g, void* cp, int cpFrame) : g_(g), cp_(cp), cpFrame_(cpFrame) {}
    bool survives(const std::vector<Input>& inputs, int untilFrame) override {
        sorted_ = inputs;
        std::stable_sort(sorted_.begin(), sorted_.end(),
                         [](Input const& a, Input const& b) { return a.frame < b.frame; });
        g_->restore(cp_);
        g_->clearFlags();
        g_->setInputs(&sorted_);
        long guard = static_cast<long>(untilFrame - cpFrame_) * 3 + 16;
        while (g_->frame() < untilFrame && !g_->dead() && !g_->done() && guard-- > 0)
            g_->stepOnce();
        bool alive = !g_->dead();
        g_->setInputs(nullptr);
        return alive;
    }
private:
    IGame* g_;
    void* cp_;
    int cpFrame_;
    std::vector<Input> sorted_;
};

bool Engine::start(IGame* game, const Macro& macro, const EngineConfig& cfg, std::string& err) {
    releaseAll();
    g_ = game;
    cfg_ = cfg;
    error_.clear();
    inputs_ = macro.inputs;
    std::stable_sort(inputs_.begin(), inputs_.end(),
                     [](Input const& a, Input const& b) { return a.frame < b.frame; });
    clicks_.clear();
    for (size_t k = 0; k < inputs_.size(); ++k)
        if (inputs_[k].down) clicks_.push_back(k);
    if (clicks_.empty()) { err = "Macro has no clicks"; state_ = State::Idle; return false; }

    results_.assign(clicks_.size(), ClickResult{});
    resultOfInput_.assign(inputs_.size(), -1);

    // number of buttons held after each input
    held_.clear();
    std::map<std::pair<int, bool>, bool> st;
    int count = 0;
    for (auto const& in : inputs_) {
        bool& s = st[{in.button, in.p2}];
        if (in.down && !s) { s = true; ++count; }
        else if (!in.down && s) { s = false; --count; }
        held_.push_back({in.frame, count});
    }

    g_->reset();
    g_->clearFlags();
    g_->setInputs(&inputs_);
    cps_.clear();
    cps_.push_back({g_->checkpoint(), g_->frame()});
    sigs_.clear();
    sigOk_.clear();
    drift_ = 0;
    recordSig(g_->frame());
    next_ = 0;
    head_ = g_->frame();
    lastCp_ = head_;
    dirty_ = false;
    baselineEnded_ = false;
    stall_ = 0;
    state_ = State::Running;
    return true;
}

void Engine::recordSig(int frame) {
    if (frame < 0 || frame > 2000000) return;
    size_t f = static_cast<size_t>(frame);
    if (f >= sigs_.size()) { sigs_.resize(f + 1, 0); sigOk_.resize(f + 1, 0); }
    sigs_[f] = g_->signature();
    sigOk_[f] = 1;
}

int Engine::unstableCount() const {
    int n = 0;
    for (size_t k = 0; k < results_.size() && k < next_; ++k)
        if (results_[k].unstable) ++n;
    return n;
}

bool Engine::heldBefore(int frame) const {
    // buttons held after all inputs with input.frame < frame
    int cnt = 0;
    for (auto const& h : held_) {
        if (h.first >= frame) break;
        cnt = h.second;
    }
    return cnt > 0;
}

const Engine::Cp& Engine::cpAtOrBefore(int frame) const {
    size_t best = 0;
    for (size_t k = 0; k < cps_.size(); ++k)
        if (cps_[k].frame <= frame) best = k;
    return cps_[best];
}

void Engine::advance(int target) {
    if (dirty_ && !rewind()) return;
    while (head_ < target && state_ == State::Running) {
        if (head_ > lastCp_ && head_ - lastCp_ >= cfg_.cpInterval && !heldBefore(head_)) {
            cps_.push_back({g_->checkpoint(), head_});
            lastCp_ = head_;
        }
        int before = g_->frame();
        g_->stepOnce();
        if (g_->frame() == before) {
            if (++stall_ > 8) { fail("Step desync - the game is not advancing at 240 TPS"); return; }
            continue;
        }
        stall_ = 0;
        head_ = g_->frame();
        recordSig(head_);
        if (g_->dead()) {
            fail("Macro dies on its own at frame " + std::to_string(head_) +
                 " - the macro does not match this level/attempt");
            return;
        }
        if (g_->done()) { baselineEnded_ = true; return; }
    }
}

bool Engine::rewind() {
    const Cp& cp = cpAtOrBefore(head_);
    g_->restore(cp.obj);
    g_->clearFlags();
    g_->setInputs(&inputs_);
    int stall = 0;
    while (g_->frame() < head_) {
        int before = g_->frame();
        g_->stepOnce();
        if (g_->frame() == before) {
            if (++stall > 8) { fail("Step desync while rewinding"); return false; }
            continue;
        }
        if (g_->dead()) { fail("Rewind desync at frame " + std::to_string(g_->frame())); return false; }
    }
    // the replay from the snapshot must land exactly where the first run was
    if (static_cast<size_t>(head_) < sigOk_.size() && sigOk_[static_cast<size_t>(head_)] &&
        g_->signature() != sigs_[static_cast<size_t>(head_)])
        ++drift_;
    dirty_ = false;
    return true;
}

void Engine::processClick(size_t k) {
    size_t idx = clicks_[k];
    int need = std::max(0, inputs_[idx].frame - cfg_.maxShift);
    const Cp& cp = cpAtOrBefore(need);
    TrialSim sim(g_, cp.obj, cp.frame);
    AnalysisOpts opts;
    opts.pairRelease = cfg_.pairRelease;
    opts.verify = cfg_.verify;
    opts.lookAhead = cfg_.lookAhead;
    opts.spanCap = cfg_.spanCap;
    ClickResult r = analyzeClick(sim, inputs_, idx, cfg_.maxShift, cfg_.horizon, opts);
    results_[k] = r;
    resultOfInput_[idx] = static_cast<int>(k);
    dirty_ = true;
}

void Engine::trimCps() {
    if (next_ >= clicks_.size()) return;
    int need = std::max(0, inputs_[clicks_[next_]].frame - cfg_.maxShift);
    size_t keep = 0;
    for (size_t k = 0; k < cps_.size(); ++k)
        if (cps_[k].frame <= need) keep = k;
    for (size_t k = 0; k < keep; ++k) g_->release(cps_[k].obj);
    cps_.erase(cps_.begin(), cps_.begin() + static_cast<long>(keep));
}

void Engine::work(double budgetSeconds) {
    if (state_ != State::Running) return;
    auto t0 = Clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - t0).count(); };
    while (state_ == State::Running) {
        if (next_ >= clicks_.size()) {
            releaseAll();
            g_->clearFlags();
            g_->setInputs(nullptr);
            state_ = State::Done;
            return;
        }
        if (elapsed() > budgetSeconds) return;
        if (!baselineEnded_ && head_ < trigger(next_)) {
            advance(std::min(trigger(next_), head_ + 256));
            continue;
        }
        processClick(next_);
        ++next_;
        trimCps();
    }
}

void Engine::fail(const std::string& msg) {
    error_ = msg;
    state_ = State::Error;
    releaseAll();
    if (g_) { g_->clearFlags(); g_->setInputs(nullptr); }
}

void Engine::releaseAll() {
    if (g_) for (auto& c : cps_) g_->release(c.obj);
    cps_.clear();
}

void Engine::abort() {
    if (state_ == State::Running) {
        releaseAll();
        if (g_) { g_->clearFlags(); g_->setInputs(nullptr); }
        state_ = State::Idle;
    }
}

void Engine::shutdown() {
    releaseAll();
    if (g_) g_->setInputs(nullptr);
    if (state_ == State::Running) state_ = State::Idle;
}

int Engine::resultOfInput(size_t inputIndex) const {
    return inputIndex < resultOfInput_.size() ? resultOfInput_[inputIndex] : -1;
}

int Engine::tierCount(int tier) const {
    int n = 0;
    for (size_t k = 0; k < results_.size() && k < next_; ++k)
        if (!results_[k].dies && tierIndex(static_cast<double>(results_[k].count)) == tier) ++n;
    return n;
}

} // namespace xfc
