/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef SCHEDULED_AVAILABILITY_MODEL_H
#define SCHEDULED_AVAILABILITY_MODEL_H

#include "link-availability-model.h"

#include "ns3/event-id.h"

#include <map>

namespace ns3
{

/**
 * @ingroup futurenet-resilience
 * @brief Availability model driven by explicit failure/recovery times.
 *
 * All times are absolute simulation times. Changes may be added before or
 * after Start(), but never in the past.
 */
class ScheduledAvailabilityModel : public LinkAvailabilityModel
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    ScheduledAvailabilityModel();
    ~ScheduledAvailabilityModel() override;

    /**
     * @brief Fail the link at @p start and recover it @p duration later.
     * @param start absolute simulation time of the failure
     * @param duration outage length, must be strictly positive
     */
    void AddDownInterval(Time start, Time duration);

    /**
     * @brief Force the link into @p state at absolute time @p at.
     * @param at absolute simulation time of the change
     * @param state the state to apply
     */
    void AddStateChange(Time at, LinkAvailability state);

    Time GetNextStateChange() const override;

  protected:
    void DoDispose() override;

  private:
    void DoStart() override;

    /**
     * @brief Schedule an event for the earliest pending change.
     *
     * Only one event is kept in the simulator at a time, so changes added
     * later are still applied in time order.
     */
    void ScheduleNextChange();

    /**
     * @brief Apply the earliest pending change and schedule the next one.
     */
    void ApplyNextChange();

    std::multimap<Time, LinkAvailability> m_pendingChanges; //!< changes not yet applied
    EventId m_nextChangeEvent;                              //!< event for the earliest change
};

} // namespace ns3

#endif /* SCHEDULED_AVAILABILITY_MODEL_H */
