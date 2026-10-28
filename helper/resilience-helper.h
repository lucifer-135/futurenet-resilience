/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef RESILIENCE_HELPER_H
#define RESILIENCE_HELPER_H

#include "ns3/failure-controller.h"
#include "ns3/link-availability-model.h"
#include "ns3/net-device-container.h"
#include "ns3/object-factory.h"

#include <vector>

namespace ns3
{

/**
 * @ingroup futurenet-resilience
 * @brief Creates FailureControllers and their availability models.
 *
 * Typical use:
 * @code
 *   ResilienceHelper helper;
 *   helper.SetAvailabilityModel("ns3::TwoStateAvailabilityModel",
 *                               "FailureProbability", DoubleValue(0.05));
 *   Ptr<FailureController> c = helper.Install(linkDevices);
 * @endcode
 *
 * IPv4 addresses must be assigned before Install(), because the helper looks
 * up the IPv4 interface of every device. Each installed model is started at
 * the current simulation time (time 0 if Install() runs before Simulator::Run).
 */
class ResilienceHelper
{
  public:
    ResilienceHelper();

    /**
     * @brief Choose the model type created when Install() gets no model.
     *
     * Default: ns3::ScheduledAvailabilityModel.
     * @tparam Ts attribute name/value pairs
     * @param type the TypeId name of a LinkAvailabilityModel subclass
     * @param args attribute name/value pairs applied to every new model
     */
    template <typename... Ts>
    void SetAvailabilityModel(const std::string& type, Ts&&... args);

    /**
     * @brief Set an attribute on every FailureController created afterwards.
     * @param name attribute name
     * @param value attribute value
     */
    void SetControllerAttribute(const std::string& name, const AttributeValue& value);

    /**
     * @brief Manage the IPv4 interfaces of @p devices as one failing link.
     *
     * Pass both devices of a point-to-point link to fail that link.
     * @param devices devices whose IPv4 interfaces fail together
     * @param model model to use; if null, one is created from the factory
     * @return the new controller
     */
    Ptr<FailureController> Install(const NetDeviceContainer& devices,
                                   Ptr<LinkAvailabilityModel> model = nullptr) const;

    /**
     * @brief Manage every non-loopback interface of @p node (node failure).
     * @param node the node to fail as a whole
     * @param model model to use; if null, one is created from the factory
     * @return the new controller
     */
    Ptr<FailureController> InstallOnNode(Ptr<Node> node,
                                         Ptr<LinkAvailabilityModel> model = nullptr) const;

    /**
     * @brief Fix the random streams of the models behind @p controllers.
     * @param controllers controllers returned by Install()/InstallOnNode()
     * @param stream first stream index to use
     * @return the number of streams used
     */
    int64_t AssignStreams(const std::vector<Ptr<FailureController>>& controllers,
                          int64_t stream) const;

  private:
    /**
     * @brief Attach a model to @p controller and schedule its start.
     * @param controller the new controller
     * @param model the model, or null to create one from the factory
     */
    void AttachModel(Ptr<FailureController> controller, Ptr<LinkAvailabilityModel> model) const;

    ObjectFactory m_modelFactory;      //!< creates availability models
    ObjectFactory m_controllerFactory; //!< creates failure controllers
};

template <typename... Ts>
void
ResilienceHelper::SetAvailabilityModel(const std::string& type, Ts&&... args)
{
    m_modelFactory.SetTypeId(type);
    m_modelFactory.Set(std::forward<Ts>(args)...);
}

} // namespace ns3

#endif /* RESILIENCE_HELPER_H */
