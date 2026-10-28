// Test helper: an EventListener that stores every event it receives.
#pragma once

#include "agora/core/events.hpp"

#include <vector>

namespace agora::test
{

class RecordingListener final : public EventListener
{
  public:
    void onAccepted(const AcceptedEvent& event) override { accepted.push_back(event); }
    void onRejected(const RejectedEvent& event) override { rejected.push_back(event); }
    void onCancelled(const CancelledEvent& event) override { cancelled.push_back(event); }
    void onTrade(const TradeEvent& event) override { trades.push_back(event); }

    void clear()
    {
        accepted.clear();
        rejected.clear();
        cancelled.clear();
        trades.clear();
    }

    std::vector<AcceptedEvent> accepted;
    std::vector<RejectedEvent> rejected;
    std::vector<CancelledEvent> cancelled;
    std::vector<TradeEvent> trades;
};

} // namespace agora::test
