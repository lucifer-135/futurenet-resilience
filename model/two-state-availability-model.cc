/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "two-state-availability-model.h"

#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TwoStateAvailabilityModel");

NS_OBJECT_ENSURE_REGISTERED(TwoStateAvailabilityModel);

TypeId
TwoStateAvailabilityModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TwoStateAvailabilityModel")
            .SetParent<LinkAvailabilityModel>()
            .SetGroupName("FuturenetResilience")
            .AddConstructor<TwoStateAvailabilityModel>()
            .AddAttribute("EvaluationInterval",
                          "Time between two random draws. Project range 10 ms - 60 s.",
                          TimeValue(Seconds(1)),
                          MakeTimeAccessor(&TwoStateAvailabilityModel::m_evaluationInterval),
                          MakeTimeChecker(MilliSeconds(10), Seconds(60)))
            .AddAttribute("FailureProbability",
                          "Probability [0,1] that an UP link fails at one evaluation.",
                          DoubleValue(0.01),
                          MakeDoubleAccessor(&TwoStateAvailabilityModel::m_failureProbability),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("RecoveryProbability",
                          "Probability [0,1] that a DOWN link recovers at one evaluation.",
                          DoubleValue(0.10),
                          MakeDoubleAccessor(&TwoStateAvailabilityModel::m_recoveryProbability),
                          MakeDoubleChecker<double>(0.0, 1.0));
    return tid;
}

TwoStateAvailabilityModel::TwoStateAvailabilityModel()
    : m_uniform(CreateObject<UniformRandomVariable>())
{
    NS_LOG_FUNCTION(this);
}

TwoStateAvailabilityModel::~TwoStateAvailabilityModel()
{
    NS_LOG_FUNCTION(this);
}

int64_t
TwoStateAvailabilityModel::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_uniform->SetStream(stream);
    return 1;
}

Time
TwoStateAvailabilityModel::GetNextStateChange() const
{
    if (!m_evaluationEvent.IsPending())
    {
        return Time::Max();
    }
    return Simulator::Now() + Simulator::GetDelayLeft(m_evaluationEvent);
}

double
TwoStateAvailabilityModel::GetSteadyStateAvailability() const
{
    double p = m_failureProbability;
    double q = m_recoveryProbability;
    if (p + q == 0.0)
    {
        // The chain never moves, so the link keeps its initial state.
        return GetState() == LinkAvailability::UP ? 1.0 : 0.0;
    }
    return q / (p + q);
}

void
TwoStateAvailabilityModel::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_evaluationEvent.Cancel();
    m_uniform = nullptr;
    LinkAvailabilityModel::DoDispose();
}

void
TwoStateAvailabilityModel::DoStart()
{
    NS_LOG_FUNCTION(this);
    m_evaluationEvent =
        Simulator::Schedule(m_evaluationInterval, &TwoStateAvailabilityModel::Evaluate, this);
}

void
TwoStateAvailabilityModel::Evaluate()
{
    // Draw exactly once per evaluation, whatever the state, so the random
    // sequence (and thus the run) only depends on seed, run and stream.
    double u = m_uniform->GetValue();
    if (GetState() == LinkAvailability::UP && u < m_failureProbability)
    {
        SetState(LinkAvailability::DOWN);
    }
    else if (GetState() == LinkAvailability::DOWN && u < m_recoveryProbability)
    {
        SetState(LinkAvailability::UP);
    }
    m_evaluationEvent =
        Simulator::Schedule(m_evaluationInterval, &TwoStateAvailabilityModel::Evaluate, this);
}

} // namespace ns3
