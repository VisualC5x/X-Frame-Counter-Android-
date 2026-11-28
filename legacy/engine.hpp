#pragma once
#include "analysis.hpp"
#include "gdr.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace xfc {

// Everything the engine needs from the running game. Implemented by the Geode glue
// in main.cpp and by a fake game in tests/.
struct IGame {
    virtual ~IGame() = default;
    virtual void reset() = 0;                  // restart level, frame() == 0
    virtual void stepOnce() = 0;               // advance exactly one physics step
    virtual int frame() const = 0;             // physics steps done since reset
    virtual void* checkpoint() = 0;            // snapshot (caller must release)
    virtual void restore(void* cp) = 0;        // load snapshot, frame() = snapshot frame
    virtual void release(void* cp) = 0;
    virtual bool dead() const = 0;
    virtual bool done() const = 0;             // level finished
    virtual void clearFlags() = 0;             // clear dead/done
    virtual void setInputs(const std::vector<Input>* sortedInputs) = 0; // null = none
    // Hash of the visible player state. Used to detect checkpoints that do not restore exactly.
    virtual uint64_t signature() const { return 0; }
};

struct EngineConfig {
    int maxShift = 30;
    int horizon = 90;
    int cpInterval = 60;
    bool pairRelease = false;   // move the release together with the press (keeps the hold length)
    bool verify = true;         // second verification pass on every click
    int lookAhead = 3;          // survive-check passes this many later clicks
    int spanCap = 6000;         // hard cap of the survive-check (frames)
};

class Engine {
public:
    enum class State { Idle, Running, Done, Error };

    bool start(IGame* game, const Macro& macro, const EngineConfig& cfg, std::string& err);
    void work(double budgetSeconds);   // call once per real frame while Running
    void abort();
    void shutdown();                   // release snapshots, go Idle

    State state() const { return state_; }
    const std::string& error() const { return error_; }
    size_t clicksTotal() const { return clicks_.size(); }
    size_t clicksDone() const { return next_; }
    const std::vector<Input>& inputs() const { return inputs_; }
    const std::vector<ClickResult>& results() const { return results_; }
    int resultOfInput(size_t inputIndex) const;
    int tierCount(int tier) const;
    int driftCount() const { return drift_; }          // times a restored checkpoint did not reproduce the baseline
    int unstableCount() const;                          // clicks whose verification pass disagreed

private:
    struct Cp { void* obj; int frame; };

    void advance(int target);
    bool rewind();
    void processClick(size_t k);
    void trimCps();
    void releaseAll();
    void fail(const std::string& msg);
    bool heldBefore(int frame) const;
    const Cp& cpAtOrBefore(int frame) const;
    int trigger(size_t k) const { return inputs_[clicks_[k]].frame + cfg_.maxShift; }

    IGame* g_ = nullptr;
    EngineConfig cfg_;
    State state_ = State::Idle;
    std::string error_;
    std::vector<Input> inputs_;
    std::vector<size_t> clicks_;
    std::vector<ClickResult> results_;
    std::vector<int> resultOfInput_;
    std::vector<std::pair<int, int>> held_;   // (frame, number of buttons held after that input)
    std::vector<Cp> cps_;
    size_t next_ = 0;
    int head_ = 0;
    int lastCp_ = 0;
    bool dirty_ = false;
    bool baselineEnded_ = false;
    int stall_ = 0;
    int drift_ = 0;
    std::vector<uint64_t> sigs_;
    std::vector<uint8_t> sigOk_;
    void recordSig(int frame);

    friend class TrialSim;
};

} // namespace xfc
