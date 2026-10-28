/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "scheduled-availability-model.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ScheduledAvailabilityModel");

NS_OBJECT_ENSURE_REGISTERED(ScheduledAvailabilityModel);

TypeId
ScheduledAvailabilityModel::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ScheduledAvailabilityModel")
                            .SetParent<LinkAvailabilityModel>()
                            .SetGroupName("FuturenetResilience")
                            .AddConstructor<ScheduledAvailabilityModel>();
    return tid;
}

ScheduledAvailabilityModel::ScheduledAvailabilityModel()
{
    NS_LOG_FUNCTION(this);
}

ScheduledAvailabilityModel::~ScheduledAvailabilityModel()
{
    NS_LOG_FUNCTION(this);
}

void
ScheduledAvailabilityModel::AddDownInterval(Time start, Time duration)
{
    NS_LOG_FUNCTION(this << start << duration);
    NS_ABORT_MSG_IF(!duration.IsStrictlyPositive(),
                    "AddDownInterval: duration must be > 0, got " << duration);
    AddStateChange(start, LinkAvailability::DOWN);
    AddStateChange(start + duration, LinkAvailability::UP);
}

void
ScheduledAvailabilityModel::AddStateChange(Time at, LinkAvailability state)
{
    NS_LOG_FUNCTION(this << at << state);
    NS_ABORT_MSG_IF(at < Simulator::Now(),
                    "AddStateChange: time " << at << " is in the past (now "
                                            << Simulator::Now() << ")");
    m_pendingChanges.emplace(at, state);
    if (IsStarted())
    {
        ScheduleNextChange();
    }
}

Time
ScheduledAvailabilityModel::GetNextStateChange() const
{
    if (m_pendingChanges.empty())
    {
        return Time::Max();
    }
    return m_pendingChanges.begin()->first;
}

void
ScheduledAvailabilityModel::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_nextChangeEvent.Cancel();
    m_pendingChanges.clear();
    LinkAvailabilityModel::DoDispose();
}

void
ScheduledAvailabilityModel::DoStart()
{
    NS_LOG_FUNCTION(this);
    ScheduleNextChange();
}

void
ScheduledAvailabilityModel::ScheduleNextChange()
{
    m_nextChangeEvent.Cancel();
    if (m_pendingChanges.empty())
    {
        return;
    }
    Time delay = m_pendingChanges.begin()->first - Simulator::Now();
    m_nextChangeEvent =
        Simulator::Schedule(delay, &ScheduledAvailabilityModel::ApplyNextChange, this);
}

void
ScheduledAvailabilityModel::ApplyNextChange()
{
    NS_LOG_FUNCTION(this);
    auto next = m_pendingChanges.begin();
    LinkAvailability state = next->second;
    m_pendingChanges.erase(next);
    SetState(state);
    ScheduleNextChange();
}

} // namespace ns3
