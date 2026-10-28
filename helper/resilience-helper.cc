/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "resilience-helper.h"

#include "ns3/abort.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ResilienceHelper");

ResilienceHelper::ResilienceHelper()
{
    m_modelFactory.SetTypeId("ns3::ScheduledAvailabilityModel");
    m_controllerFactory.SetTypeId("ns3::FailureController");
}

void
ResilienceHelper::SetControllerAttribute(const std::string& name, const AttributeValue& value)
{
    m_controllerFactory.Set(name, value);
}

Ptr<FailureController>
ResilienceHelper::Install(const NetDeviceContainer& devices, Ptr<LinkAvailabilityModel> model) const
{
    NS_ABORT_MSG_IF(devices.GetN() == 0, "ResilienceHelper::Install: no devices given");
    Ptr<FailureController> controller = m_controllerFactory.Create<FailureController>();
    for (auto it = devices.Begin(); it != devices.End(); ++it)
    {
        Ptr<NetDevice> device = *it;
        Ptr<Node> node = device->GetNode();
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        NS_ABORT_MSG_IF(!ipv4, "ResilienceHelper::Install: node " << node->GetId() << " has no Ipv4");
        int32_t interface = ipv4->GetInterfaceForDevice(device);
        NS_ABORT_MSG_IF(interface < 0,
                        "ResilienceHelper::Install: device on node "
                            << node->GetId() << " has no IPv4 interface; assign addresses first");
        controller->AddManagedInterface(node, static_cast<uint32_t>(interface));
    }
    AttachModel(controller, model);
    return controller;
}

Ptr<FailureController>
ResilienceHelper::InstallOnNode(Ptr<Node> node, Ptr<LinkAvailabilityModel> model) const
{
    NS_ABORT_MSG_IF(!node, "ResilienceHelper::InstallOnNode: node is null");
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    NS_ABORT_MSG_IF(!ipv4,
                    "ResilienceHelper::InstallOnNode: node " << node->GetId() << " has no Ipv4");
    NS_ABORT_MSG_IF(ipv4->GetNInterfaces() < 2,
                    "ResilienceHelper::InstallOnNode: node " << node->GetId()
                                                             << " has only the loopback");
    Ptr<FailureController> controller = m_controllerFactory.Create<FailureController>();
    // Interface 0 is the loopback and is never failed.
    for (uint32_t i = 1; i < ipv4->GetNInterfaces(); ++i)
    {
        controller->AddManagedInterface(node, i);
    }
    AttachModel(controller, model);
    return controller;
}

int64_t
ResilienceHelper::AssignStreams(const std::vector<Ptr<FailureController>>& controllers,
                                int64_t stream) const
{
    int64_t current = stream;
    for (const auto& controller : controllers)
    {
        Ptr<LinkAvailabilityModel> model = controller->GetAvailabilityModel();
        if (model)
        {
            current += model->AssignStreams(current);
        }
    }
    return current - stream;
}

void
ResilienceHelper::AttachModel(Ptr<FailureController> controller,
                              Ptr<LinkAvailabilityModel> model) const
{
    if (!model)
    {
        model = m_modelFactory.Create<LinkAvailabilityModel>();
    }
    controller->SetAvailabilityModel(model);
    // Deferred to an event so the user can still add scheduled changes or
    // assign streams after Install() and before the simulation starts.
    Simulator::ScheduleNow(&LinkAvailabilityModel::Start, model);
}

} // namespace ns3
