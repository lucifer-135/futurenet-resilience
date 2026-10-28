/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "failure-controller.h"

#include "ns3/abort.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FailureController");

NS_OBJECT_ENSURE_REGISTERED(FailureController);

TypeId
FailureController::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::FailureController")
            .SetParent<Object>()
            .SetGroupName("FuturenetResilience")
            .AddConstructor<FailureController>()
            .AddAttribute("RecoveryDelay",
                          "Time between the model reporting UP and the interfaces "
                          "actually coming back up (e.g. reboot time). Range 0 - 300 s.",
                          TimeValue(Seconds(0)),
                          MakeTimeAccessor(&FailureController::m_recoveryDelay),
                          MakeTimeChecker(Seconds(0), Seconds(300)))
            .AddTraceSource("LinkDown",
                            "A managed interface was set down.",
                            MakeTraceSourceAccessor(&FailureController::m_linkDownTrace),
                            "ns3::FailureController::LinkEventTracedCallback")
            .AddTraceSource("LinkUp",
                            "A managed interface was set up again.",
                            MakeTraceSourceAccessor(&FailureController::m_linkUpTrace),
                            "ns3::FailureController::LinkEventTracedCallback")
            .AddTraceSource("RecoveryCompleted",
                            "A managed interface recovered; reports the outage length.",
                            MakeTraceSourceAccessor(&FailureController::m_recoveryCompletedTrace),
                            "ns3::FailureController::RecoveryTracedCallback");
    return tid;
}

FailureController::FailureController()
    : m_state(LinkAvailability::UP)
{
    NS_LOG_FUNCTION(this);
}

FailureController::~FailureController()
{
    NS_LOG_FUNCTION(this);
}

void
FailureController::AddManagedInterface(Ptr<Node> node, uint32_t interface)
{
    NS_LOG_FUNCTION(this << node << interface);
    NS_ABORT_MSG_IF(!node, "AddManagedInterface: node is null");
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    NS_ABORT_MSG_IF(!ipv4, "AddManagedInterface: node " << node->GetId() << " has no Ipv4 stack");
    NS_ABORT_MSG_IF(interface == 0, "AddManagedInterface: interface 0 is the loopback");
    NS_ABORT_MSG_IF(interface >= ipv4->GetNInterfaces(),
                    "AddManagedInterface: node " << node->GetId() << " has no interface "
                                                 << interface);
    m_interfaces.push_back({node, interface});
}

uint32_t
FailureController::GetNManagedInterfaces() const
{
    return static_cast<uint32_t>(m_interfaces.size());
}

void
FailureController::SetAvailabilityModel(Ptr<LinkAvailabilityModel> model)
{
    NS_LOG_FUNCTION(this << model);
    NS_ABORT_MSG_IF(!model, "SetAvailabilityModel: model is null");
    if (m_model)
    {
        m_model->SetStateChangeCallback(MakeNullCallback<void, LinkAvailability>());
    }
    m_model = model;
    m_model->SetStateChangeCallback(MakeCallback(&FailureController::ApplyState, this));
}

Ptr<LinkAvailabilityModel>
FailureController::GetAvailabilityModel() const
{
    return m_model;
}

void
FailureController::ApplyState(LinkAvailability state)
{
    NS_LOG_FUNCTION(this << state);
    if (state == LinkAvailability::DOWN)
    {
        // A failure during RecoveryDelay means the link never came back.
        m_recoveryEvent.Cancel();
        if (m_state == LinkAvailability::DOWN)
        {
            return;
        }
        m_state = LinkAvailability::DOWN;
        m_downSince = Simulator::Now();
        for (const auto& managed : m_interfaces)
        {
            managed.node->GetObject<Ipv4>()->SetDown(managed.interface);
            m_linkDownTrace(managed.node->GetId(), managed.interface);
        }
        return;
    }

    if (m_state == LinkAvailability::UP || m_recoveryEvent.IsPending())
    {
        return;
    }
    if (m_recoveryDelay.IsZero())
    {
        BringInterfacesUp();
    }
    else
    {
        m_recoveryEvent =
            Simulator::Schedule(m_recoveryDelay, &FailureController::BringInterfacesUp, this);
    }
}

LinkAvailability
FailureController::GetCurrentState() const
{
    return m_state;
}

void
FailureController::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_recoveryEvent.Cancel();
    if (m_model)
    {
        m_model->SetStateChangeCallback(MakeNullCallback<void, LinkAvailability>());
        m_model = nullptr;
    }
    m_interfaces.clear();
    Object::DoDispose();
}

void
FailureController::BringInterfacesUp()
{
    NS_LOG_FUNCTION(this);
    m_state = LinkAvailability::UP;
    Time disruption = Simulator::Now() - m_downSince;
    for (const auto& managed : m_interfaces)
    {
        managed.node->GetObject<Ipv4>()->SetUp(managed.interface);
        m_linkUpTrace(managed.node->GetId(), managed.interface);
        m_recoveryCompletedTrace(managed.node->GetId(), managed.interface, disruption);
    }
}

} // namespace ns3
