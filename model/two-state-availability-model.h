/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef TWO_STATE_AVAILABILITY_MODEL_H
#define TWO_STATE_AVAILABILITY_MODEL_H

#include "link-availability-model.h"

#include "ns3/event-id.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

/**
 * @ingroup futurenet-resilience
 * @brief Random UP/DOWN model (discrete-time two-state Markov chain).
 *
 * Every EvaluationInterval one uniform number u in [0,1) is drawn:
 *  - if UP and u < FailureProbability, the link goes DOWN;
 *  - if DOWN and u < RecoveryProbability, the link comes back UP.
 *
 * Only these per-evaluation probabilities are used (no mean up/down times),
 * as required by the SRS. The equivalent means follow from the geometric
 * distribution: mean up time = EvaluationInterval / FailureProbability and
 * mean down time = EvaluationInterval / RecoveryProbability.
 */
class TwoStateAvailabilityModel : public LinkAvailabilityModel
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    TwoStateAvailabilityModel();
    ~TwoStateAvailabilityModel() override;

    /**
     * @brief Fix the random stream so runs are reproducible.
     * @param stream first stream index to use
     * @return the number of streams used (1)
     */
    int64_t AssignStreams(int64_t stream) override;

    /**
     * @return the time of the next evaluation (the earliest possible change),
     *         or Time::Max() before Start()
     */
    Time GetNextStateChange() const override;

    /**
     * @brief Long-run fraction of time the link is UP.
     *
     * Closed form for the two-state chain: q / (p + q), with p the failure
     * and q the recovery probability. Used to validate simulation output.
     * @return availability in [0,1]
     */
    double GetSteadyStateAvailability() const;

  protected:
    void DoDispose() override;

  private:
    void DoStart() override;

    /**
     * @brief Draw one random number, maybe change state, schedule next draw.
     */
    void Evaluate();

    Time m_evaluationInterval;              //!< time between two draws
    double m_failureProbability;            //!< P(UP -> DOWN) per evaluation
    double m_recoveryProbability;           //!< P(DOWN -> UP) per evaluation
    Ptr<UniformRandomVariable> m_uniform;   //!< source of randomness
    EventId m_evaluationEvent;              //!< next scheduled Evaluate()
};

} // namespace ns3

#endif /* TWO_STATE_AVAILABILITY_MODEL_H */
