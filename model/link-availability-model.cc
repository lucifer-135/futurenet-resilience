/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "link-availability-model.h"

#include "ns3/abort.h"
#include "ns3/enum.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LinkAvailabilityModel");

NS_OBJECT_ENSURE_REGISTERED(LinkAvailabilityModel);

std::ostream&
operator<<(std::ostream& os, LinkAvailability state)
{
    switch (state)
    {
    case LinkAvailability::UP:
        return os << "UP";
    case LinkAvailability::DOWN:
        return os << "DOWN";
    }
    return os << "UNKNOWN";
}

TypeId
LinkAvailabilityModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::LinkAvailabilityModel")
            .SetParent<Object>()
            .SetGroupName("FuturenetResilience")
            .AddAttribute("InitialState",
                          "State applied when the model is started.",
                          EnumValue(LinkAvailability::UP),
                          MakeEnumAccessor<LinkAvailability>(&LinkAvailabilityModel::m_initialState),
                          MakeEnumChecker(LinkAvailability::UP, "UP", LinkAvailability::DOWN, "DOWN"));
    return tid;
}

LinkAvailabilityModel::LinkAvailabilityModel()
    : m_initialState(LinkAvailability::UP),
      m_state(LinkAvailability::UP),
      m_started(false)
{
    NS_LOG_FUNCTION(this);
}

LinkAvailabilityModel::~LinkAvailabilityModel()
{
    NS_LOG_FUNCTION(this);
}

LinkAvailability
LinkAvailabilityModel::GetState() const
{
    return m_state;
}

void
LinkAvailabilityModel::Start()
{
    NS_LOG_FUNCTION(this);
    NS_ABORT_MSG_IF(m_started, "LinkAvailabilityModel::Start() called twice");
    m_started = true;
    SetState(m_initialState);
    DoStart();
}

int64_t
LinkAvailabilityModel::AssignStreams(int64_t /* stream */)
{
    return 0;
}

void
LinkAvailabilityModel::SetStateChangeCallback(StateChangeCallback cb)
{
    m_stateChangeCallback = cb;
}

void
LinkAvailabilityModel::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_stateChangeCallback = MakeNullCallback<void, LinkAvailability>();
    Object::DoDispose();
}

void
LinkAvailabilityModel::SetState(LinkAvailability state)
{
    if (state == m_state)
    {
        return;
    }
    NS_LOG_INFO("State " << m_state << " -> " << state);
    m_state = state;
    if (!m_stateChangeCallback.IsNull())
    {
        m_stateChangeCallback(state);
    }
}

bool
LinkAvailabilityModel::IsStarted() const
{
    return m_started;
}

} // namespace ns3
