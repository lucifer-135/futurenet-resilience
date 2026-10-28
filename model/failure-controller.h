/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef FAILURE_CONTROLLER_H
#define FAILURE_CONTROLLER_H

#include "link-availability-model.h"

#include "ns3/event-id.h"
#include "ns3/node.h"
#include "ns3/traced-callback.h"

#include <vector>

namespace ns3
{

/**
 * @ingroup futurenet-resilience
 * @brief Applies UP/DOWN states to a group of IPv4 interfaces.
 *
 * All managed interfaces fail and recover together. Manage both ends of a
 * point-to-point link to fail that link, or every interface of one node to
 * fail the whole node.
 *
 * Failure is done with Ipv4::SetDown()/SetUp(): IPv4 then drops packets on
 * the interface and routing protocols are notified, so rerouting is left to
 * whatever routing protocol the scenario uses.
 */
class FailureController : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    FailureController();
    ~FailureController() override;

    /**
     * TracedCallback signature for LinkDown and LinkUp.
     * @param [in] nodeId the node owning the interface
     * @param [in] interfaceId the IPv4 interface index
     */
    typedef void (*LinkEventTracedCallback)(uint32_t nodeId, uint32_t interfaceId);

    /**
     * TracedCallback signature for RecoveryCompleted.
     * @param [in] nodeId the node owning the interface
     * @param [in] interfaceId the IPv4 interface index
     * @param [in] disruption how long the interface was down
     */
    typedef void (*RecoveryTracedCallback)(uint32_t nodeId,
                                           uint32_t interfaceId,
                                           Time disruption);

    /**
     * @brief Add an interface to the group controlled by this object.
     * @param node node with an Ipv4 stack
     * @param interface IPv4 interface index (not 0, which is the loopback)
     */
    void AddManagedInterface(Ptr<Node> node, uint32_t interface);

    /**
     * @return number of managed interfaces
     */
    uint32_t GetNManagedInterfaces() const;

    /**
     * @brief Let @p model decide when the interfaces fail and recover.
     * @param model the availability model; the caller still has to Start() it
     */
    void SetAvailabilityModel(Ptr<LinkAvailabilityModel> model);

    /**
     * @return the availability model, or nullptr if none was set
     */
    Ptr<LinkAvailabilityModel> GetAvailabilityModel() const;

    /**
     * @brief Bring all managed interfaces down or up.
     *
     * Normally called by the availability model, but also public so another
     * module (e.g. an agent) can inject failures directly. Recovery waits for
     * RecoveryDelay; a new failure during that wait cancels the recovery.
     * @param state the requested state
     */
    void ApplyState(LinkAvailability state);

    /**
     * @return the state actually applied to the interfaces
     */
    LinkAvailability GetCurrentState() const;

  protected:
    void DoDispose() override;

  private:
    /// One managed (node, interface) pair.
    struct ManagedInterface
    {
        Ptr<Node> node;     //!< node owning the interface
        uint32_t interface; //!< IPv4 interface index
    };

    /**
     * @brief Set every managed interface up and fire the recovery traces.
     */
    void BringInterfacesUp();

    std::vector<ManagedInterface> m_interfaces; //!< interfaces failing together
    Ptr<LinkAvailabilityModel> m_model;         //!< decides when to fail
    LinkAvailability m_state;                   //!< state applied to interfaces
    Time m_downSince;                           //!< start of the current outage
    Time m_recoveryDelay;                       //!< extra time before interfaces come up
    EventId m_recoveryEvent;                    //!< pending delayed recovery

    TracedCallback<uint32_t, uint32_t> m_linkDownTrace;             //!< LinkDown trace
    TracedCallback<uint32_t, uint32_t> m_linkUpTrace;               //!< LinkUp trace
    TracedCallback<uint32_t, uint32_t, Time> m_recoveryCompletedTrace; //!< RecoveryCompleted trace
};

} // namespace ns3

#endif /* FAILURE_CONTROLLER_H */
