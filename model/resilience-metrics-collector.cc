/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "resilience-metrics-collector.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <iomanip>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ResilienceMetricsCollector");

NS_OBJECT_ENSURE_REGISTERED(ResilienceMetricsCollector);

TypeId
ResilienceMetricsCollector::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ResilienceMetricsCollector")
                            .SetParent<Object>()
                            .SetGroupName("FuturenetResilience")
                            .AddConstructor<ResilienceMetricsCollector>();
    return tid;
}

ResilienceMetricsCollector::ResilienceMetricsCollector()
    : m_failureCount(0),
      m_recoveryCount(0)
{
    NS_LOG_FUNCTION(this);
}

ResilienceMetricsCollector::~ResilienceMetricsCollector()
{
    NS_LOG_FUNCTION(this);
}

void
ResilienceMetricsCollector::Monitor(Ptr<FailureController> controller)
{
    NS_LOG_FUNCTION(this << controller);
    NS_ABORT_MSG_IF(!controller, "Monitor: controller is null");
    // The trace context carries the link index, so one sink serves all links.
    std::string context = std::to_string(m_links.size());
    m_links.push_back({controller, false, Seconds(0)});
    controller->TraceConnect("LinkDown",
                             context,
                             MakeCallback(&ResilienceMetricsCollector::NotifyLinkDown, this));
    controller->TraceConnect(
        "RecoveryCompleted",
        context,
        MakeCallback(&ResilienceMetricsCollector::NotifyRecoveryCompleted, this));
}

void
ResilienceMetricsCollector::EnableCsvOutput(const std::string& fileName)
{
    NS_LOG_FUNCTION(this << fileName);
    m_csv.open(fileName);
    NS_ABORT_MSG_IF(!m_csv.is_open(), "EnableCsvOutput: cannot open " << fileName);
    m_csv << std::fixed << std::setprecision(6);
    m_csv << "time_s,module,event,node_id,peer_id,flow_id,value1,value2,unit,detail,"
             "interface_id,state,cause,disruption_ms\n";
}

uint32_t
ResilienceMetricsCollector::GetFailureCount() const
{
    return m_failureCount;
}

uint32_t
ResilienceMetricsCollector::GetRecoveryCount() const
{
    return m_recoveryCount;
}

Time
ResilienceMetricsCollector::GetTotalDisruptionTime() const
{
    Time total = m_completedDisruption;
    for (const auto& link : m_links)
    {
        if (link.down)
        {
            total += Simulator::Now() - link.downSince;
        }
    }
    return total;
}

Time
ResilienceMetricsCollector::GetLastRecoveryTime() const
{
    return m_lastRecoveryTime;
}

void
ResilienceMetricsCollector::DoDispose()
{
    NS_LOG_FUNCTION(this);
    if (m_csv.is_open())
    {
        m_csv.close();
    }
    m_links.clear();
    Object::DoDispose();
}

void
ResilienceMetricsCollector::NotifyLinkDown(std::string context,
                                           uint32_t nodeId,
                                           uint32_t interfaceId)
{
    auto index = static_cast<uint32_t>(std::stoul(context));
    MonitoredLink& link = m_links.at(index);
    // A controller fires LinkDown once per interface; count the failure once.
    if (!link.down)
    {
        link.down = true;
        link.downSince = Simulator::Now();
        ++m_failureCount;
    }
    WriteCsvRow(index, "LinkDown", nodeId, interfaceId, LinkAvailability::DOWN, Seconds(0));
}

void
ResilienceMetricsCollector::NotifyRecoveryCompleted(std::string context,
                                                    uint32_t nodeId,
                                                    uint32_t interfaceId,
                                                    Time disruption)
{
    auto index = static_cast<uint32_t>(std::stoul(context));
    MonitoredLink& link = m_links.at(index);
    if (link.down)
    {
        link.down = false;
        ++m_recoveryCount;
        m_completedDisruption += disruption;
        m_lastRecoveryTime = Simulator::Now();
    }
    WriteCsvRow(index,
                "RecoveryCompleted",
                nodeId,
                interfaceId,
                LinkAvailability::UP,
                disruption);
}

void
ResilienceMetricsCollector::WriteCsvRow(uint32_t linkIndex,
                                        const std::string& event,
                                        uint32_t nodeId,
                                        uint32_t interfaceId,
                                        LinkAvailability state,
                                        Time disruption)
{
    if (!m_csv.is_open())
    {
        return;
    }
    double disruptionMs = disruption.GetSeconds() * 1000.0;
    // peer_id, flow_id and value2 do not apply to link events and stay empty.
    m_csv << Simulator::Now().GetSeconds() << ",resilience," << event << "," << nodeId << ",,,"
          << disruptionMs << ",,ms,link=" << linkIndex << "," << interfaceId << "," << state << ","
          << GetCause(m_links.at(linkIndex)) << "," << disruptionMs << "\n";
}

std::string
ResilienceMetricsCollector::GetCause(const MonitoredLink& link)
{
    Ptr<LinkAvailabilityModel> model = link.controller->GetAvailabilityModel();
    if (!model)
    {
        return "manual";
    }
    std::string name = model->GetInstanceTypeId().GetName();
    const std::string prefix = "ns3::";
    if (name.compare(0, prefix.size(), prefix) == 0)
    {
        name = name.substr(prefix.size());
    }
    return name;
}

} // namespace ns3
