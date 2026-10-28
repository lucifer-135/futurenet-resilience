/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef LINK_AVAILABILITY_MODEL_H
#define LINK_AVAILABILITY_MODEL_H

#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

#include <ostream>

/**
 * @defgroup futurenet-resilience FutureNet link failure and resilience
 *
 * Failure injection (scheduled or stochastic), interface up/down control and
 * resilience metrics for ns-3 experiments.
 */

namespace ns3
{

/**
 * @ingroup futurenet-resilience
 * Availability state of a managed link or interface.
 */
enum class LinkAvailability
{
    UP,  //!< Link carries traffic normally
    DOWN //!< Link has failed; its IPv4 interfaces are set down
};

/**
 * @brief Stream insertion operator, prints "UP" or "DOWN".
 * @param os the output stream
 * @param state the state to print
 * @return the output stream
 */
std::ostream& operator<<(std::ostream& os, LinkAvailability state);

/**
 * @ingroup futurenet-resilience
 * @brief Base class that decides WHEN a link is up or down.
 *
 * A model only decides the timing of state changes. Applying a state to real
 * interfaces is the job of FailureController, so a model can be replaced
 * (scheduled, random, or later an external agent) without touching the
 * mechanism that brings interfaces down.
 */
class LinkAvailabilityModel : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    LinkAvailabilityModel();
    ~LinkAvailabilityModel() override;

    /// Signature of the callback notified on every state change.
    typedef Callback<void, LinkAvailability> StateChangeCallback;

    /**
     * @return the current availability state
     */
    LinkAvailability GetState() const;

    /**
     * @brief Start the model.
     *
     * Applies the InitialState attribute and lets the subclass schedule its
     * first state change. Call at most once.
     */
    void Start();

    /**
     * @return the absolute simulation time of the next possible state change,
     *         or Time::Max() if no change is expected
     */
    virtual Time GetNextStateChange() const = 0;

    /**
     * @brief Fix the random streams used by this model.
     *
     * Deterministic models use no stream; random models override this.
     * @param stream first stream index to use
     * @return the number of streams used
     */
    virtual int64_t AssignStreams(int64_t stream);

    /**
     * @brief Set the callback notified when the state changes.
     * @param cb the callback
     */
    void SetStateChangeCallback(StateChangeCallback cb);

  protected:
    void DoDispose() override;

    /**
     * @brief Change the state and notify the callback.
     *
     * Does nothing if the state is unchanged, so listeners only see real
     * transitions.
     * @param state the new state
     */
    void SetState(LinkAvailability state);

    /**
     * @return true once Start() has been called
     */
    bool IsStarted() const;

  private:
    /**
     * @brief Subclass hook, called by Start() after the initial state is set.
     */
    virtual void DoStart() = 0;

    LinkAvailability m_initialState;          //!< state applied by Start()
    LinkAvailability m_state;                 //!< current state
    bool m_started;                           //!< true after Start()
    StateChangeCallback m_stateChangeCallback; //!< listener (usually a FailureController)
};

} // namespace ns3

#endif /* LINK_AVAILABILITY_MODEL_H */
