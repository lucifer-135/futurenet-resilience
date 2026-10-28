/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/double.h"
#include "ns3/failure-controller.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4.h"
#include "ns3/resilience-helper.h"
#include "ns3/resilience-metrics-collector.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/scheduled-availability-model.h"
#include "ns3/simple-net-device-helper.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/test.h"
#include "ns3/two-state-availability-model.h"

#include <vector>

using namespace ns3;

namespace
{

/**
 * Build two nodes joined by one SimpleChannel, with IPv4 addresses.
 * Each node gets IPv4 interface 1 on that channel (0 is the loopback).
 * @param nodes output: the two nodes
 * @param devices output: their devices
 */
void
BuildTwoNodeNetwork(NodeContainer& nodes, NetDeviceContainer& devices)
{
    nodes.Create(2);
    SimpleNetDeviceHelper simple;
    devices = simple.Install(nodes);
    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper address("10.0.0.0", "255.255.255.0");
    address.Assign(devices);
}

/**
 * @param node a node with Ipv4
 * @param interface IPv4 interface index
 * @return true if the interface is up
 */
bool
IsInterfaceUp(Ptr<Node> node, uint32_t interface)
{
    return node->GetObject<Ipv4>()->IsUp(interface);
}

} // namespace

/**
 * @ingroup futurenet-resilience
 * T1: objects start in the documented default state.
 */
class DefaultValuesTestCase : public TestCase
{
  public:
    DefaultValuesTestCase()
        : TestCase("T1 default values")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<TwoStateAvailabilityModel> model = CreateObject<TwoStateAvailabilityModel>();
        TimeValue interval;
        model->GetAttribute("EvaluationInterval", interval);
        NS_TEST_ASSERT_MSG_EQ(interval.Get(), Seconds(1), "EvaluationInterval default");
        DoubleValue probability;
        model->GetAttribute("FailureProbability", probability);
        NS_TEST_ASSERT_MSG_EQ_TOL(probability.Get(), 0.01, 1e-12, "FailureProbability default");
        model->GetAttribute("RecoveryProbability", probability);
        NS_TEST_ASSERT_MSG_EQ_TOL(probability.Get(), 0.10, 1e-12, "RecoveryProbability default");
        NS_TEST_ASSERT_MSG_EQ(model->GetNextStateChange(),
                              Time::Max(),
                              "No change expected before Start()");

        model->Start();
        NS_TEST_ASSERT_MSG_EQ(model->GetState(), LinkAvailability::UP, "InitialState default");
        NS_TEST_ASSERT_MSG_EQ(model->GetNextStateChange(),
                              Seconds(1),
                              "First evaluation one interval after Start()");

        Ptr<FailureController> controller = CreateObject<FailureController>();
        NS_TEST_ASSERT_MSG_EQ(controller->GetCurrentState(),
                              LinkAvailability::UP,
                              "Controller starts UP");
        TimeValue recoveryDelay;
        controller->GetAttribute("RecoveryDelay", recoveryDelay);
        NS_TEST_ASSERT_MSG_EQ(recoveryDelay.Get(), Seconds(0), "RecoveryDelay default");
        Simulator::Destroy();
    }
};

/**
 * @ingroup futurenet-resilience
 * T2: attribute checkers reject values outside the project ranges.
 */
class AttributeValidationTestCase : public TestCase
{
  public:
    AttributeValidationTestCase()
        : TestCase("T2 attribute validation")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<TwoStateAvailabilityModel> model = CreateObject<TwoStateAvailabilityModel>();
        NS_TEST_ASSERT_MSG_EQ(model->SetAttributeFailSafe("FailureProbability", DoubleValue(1.5)),
                              false,
                              "Probability above 1 accepted");
        NS_TEST_ASSERT_MSG_EQ(
            model->SetAttributeFailSafe("RecoveryProbability", DoubleValue(-0.1)),
            false,
            "Negative probability accepted");
        NS_TEST_ASSERT_MSG_EQ(
            model->SetAttributeFailSafe("EvaluationInterval", TimeValue(MilliSeconds(1))),
            false,
            "Interval below 10 ms accepted");
        NS_TEST_ASSERT_MSG_EQ(
            model->SetAttributeFailSafe("EvaluationInterval", TimeValue(Seconds(61))),
            false,
            "Interval above 60 s accepted");
        NS_TEST_ASSERT_MSG_EQ(model->SetAttributeFailSafe("FailureProbability", DoubleValue(1.0)),
                              true,
                              "Boundary probability 1 rejected");

        Ptr<FailureController> controller = CreateObject<FailureController>();
        NS_TEST_ASSERT_MSG_EQ(
            controller->SetAttributeFailSafe("RecoveryDelay", TimeValue(Seconds(301))),
            false,
            "RecoveryDelay above 300 s accepted");

        Ptr<ScheduledAvailabilityModel> scheduled = CreateObject<ScheduledAvailabilityModel>();
        scheduled->SetAttribute("InitialState", StringValue("DOWN"));
        scheduled->Start();
        NS_TEST_ASSERT_MSG_EQ(scheduled->GetState(),
                              LinkAvailability::DOWN,
                              "InitialState DOWN not applied");
        Simulator::Destroy();
    }
};

/**
 * @ingroup futurenet-resilience
 * SRS T01: scheduled state changes occur at exact configured times.
 */
class ScheduledTimesTestCase : public TestCase
{
  public:
    ScheduledTimesTestCase()
        : TestCase("SRS-T01 scheduled changes at exact times")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<ScheduledAvailabilityModel> model = CreateObject<ScheduledAvailabilityModel>();
        model->AddDownInterval(Seconds(2), Seconds(3));
        model->AddStateChange(Seconds(7), LinkAvailability::DOWN);
        model->SetStateChangeCallback(MakeCallback(&ScheduledTimesTestCase::Record, this));
        model->Start();
        NS_TEST_ASSERT_MSG_EQ(model->GetNextStateChange(), Seconds(2), "First change time");

        Simulator::Stop(Seconds(10));
        Simulator::Run();

        NS_TEST_ASSERT_MSG_EQ(m_times.size(), 3U, "Wrong number of changes");
        NS_TEST_ASSERT_MSG_EQ(m_times[0], Seconds(2), "Failure time");
        NS_TEST_ASSERT_MSG_EQ(m_states[0], LinkAvailability::DOWN, "Failure state");
        NS_TEST_ASSERT_MSG_EQ(m_times[1], Seconds(5), "Recovery time");
        NS_TEST_ASSERT_MSG_EQ(m_states[1], LinkAvailability::UP, "Recovery state");
        NS_TEST_ASSERT_MSG_EQ(m_times[2], Seconds(7), "Second failure time");
        NS_TEST_ASSERT_MSG_EQ(m_states[2], LinkAvailability::DOWN, "Second failure state");
        NS_TEST_ASSERT_MSG_EQ(model->GetNextStateChange(), Time::Max(), "Nothing left");
        Simulator::Destroy();
    }

    /**
     * Model callback.
     * @param state new state
     */
    void Record(LinkAvailability state)
    {
        m_times.push_back(Simulator::Now());
        m_states.push_back(state);
    }

    std::vector<Time> m_times;               //!< change times
    std::vector<LinkAvailability> m_states; //!< new states
};

/**
 * @ingroup futurenet-resilience
 * SRS T02 / T6: IPv4 interface state matches the controller state.
 */
class InterfaceStateTestCase : public TestCase
{
  public:
    InterfaceStateTestCase()
        : TestCase("SRS-T02 interface state matches controller state")
    {
    }

  private:
    void DoRun() override
    {
        NetDeviceContainer devices;
        BuildTwoNodeNetwork(m_nodes, devices);

        Ptr<ScheduledAvailabilityModel> model = CreateObject<ScheduledAvailabilityModel>();
        model->AddDownInterval(Seconds(1), Seconds(2));
        ResilienceHelper helper;
        m_controller = helper.Install(devices, model);
        NS_TEST_ASSERT_MSG_EQ(m_controller->GetNManagedInterfaces(), 2U, "Both ends managed");

        Simulator::Schedule(Seconds(0.5),
                            &InterfaceStateTestCase::Check,
                            this,
                            LinkAvailability::UP);
        Simulator::Schedule(Seconds(2), &InterfaceStateTestCase::Check, this, LinkAvailability::DOWN);
        Simulator::Schedule(Seconds(4), &InterfaceStateTestCase::Check, this, LinkAvailability::UP);
        Simulator::Stop(Seconds(5));
        Simulator::Run();
        Simulator::Destroy();
    }

    /**
     * Compare controller and IPv4 interface states.
     * @param expected the state both should be in
     */
    void Check(LinkAvailability expected)
    {
        NS_TEST_EXPECT_MSG_EQ(m_controller->GetCurrentState(), expected, "Controller state");
        bool expectUp = (expected == LinkAvailability::UP);
        for (uint32_t i = 0; i < m_nodes.GetN(); ++i)
        {
            NS_TEST_EXPECT_MSG_EQ(IsInterfaceUp(m_nodes.Get(i), 1),
                                  expectUp,
                                  "Interface state of node " << i);
        }
    }

    NodeContainer m_nodes;               //!< the two nodes
    Ptr<FailureController> m_controller; //!< controller under test
};

/**
 * @ingroup futurenet-resilience
 * SRS T03 / T5: same seed, run and stream give the same failure sequence.
 */
class ReproducibilityTestCase : public TestCase
{
  public:
    ReproducibilityTestCase()
        : TestCase("SRS-T03 stochastic model is reproducible")
    {
    }

  private:
    void DoRun() override
    {
        RngSeedManager::SetSeed(1);
        RngSeedManager::SetRun(1);
        std::vector<Time> first = RunOnce();
        std::vector<Time> second = RunOnce();
        NS_TEST_ASSERT_MSG_GT(first.size(), 0U, "Expected some state changes");
        NS_TEST_ASSERT_MSG_EQ((first == second), true, "Sequences differ for the same stream");
    }

    /**
     * Run one random model for 200 s.
     * @return the times of all state changes
     */
    std::vector<Time> RunOnce()
    {
        m_changes.clear();
        Ptr<TwoStateAvailabilityModel> model = CreateObject<TwoStateAvailabilityModel>();
        model->SetAttribute("FailureProbability", DoubleValue(0.3));
        model->SetAttribute("RecoveryProbability", DoubleValue(0.3));
        model->AssignStreams(7);
        model->SetStateChangeCallback(MakeCallback(&ReproducibilityTestCase::Record, this));
        model->Start();
        Simulator::Stop(Seconds(200));
        Simulator::Run();
        Simulator::Destroy();
        return m_changes;
    }

    /**
     * Model callback.
     * @param state new state (unused, times are enough)
     */
    void Record(LinkAvailability /* state */)
    {
        m_changes.push_back(Simulator::Now());
    }

    std::vector<Time> m_changes; //!< change times of the current run
};

/**
 * @ingroup futurenet-resilience
 * SRS T04: zero failure probability causes no failure.
 */
class ZeroFailureProbabilityTestCase : public TestCase
{
  public:
    ZeroFailureProbabilityTestCase()
        : TestCase("SRS-T04 zero failure probability causes no failure")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<TwoStateAvailabilityModel> model = CreateObject<TwoStateAvailabilityModel>();
        model->SetAttribute("FailureProbability", DoubleValue(0.0));
        model->AssignStreams(3);
        model->SetStateChangeCallback(
            MakeCallback(&ZeroFailureProbabilityTestCase::Record, this));
        model->Start();
        Simulator::Stop(Seconds(1000));
        Simulator::Run();
        NS_TEST_ASSERT_MSG_EQ(m_changeCount, 0U, "Link changed state");
        NS_TEST_ASSERT_MSG_EQ(model->GetState(), LinkAvailability::UP, "Link not UP");
        Simulator::Destroy();
    }

    /**
     * Model callback.
     * @param state new state (unused)
     */
    void Record(LinkAvailability /* state */)
    {
        ++m_changeCount;
    }

    uint32_t m_changeCount{0}; //!< number of state changes
};

/**
 * @ingroup futurenet-resilience
 * SRS T05 / T4: the collector measures a known outage, and the controller
 * fires its trace sources once per interface.
 */
class MetricsTestCase : public TestCase
{
  public:
    /**
     * @param recoveryDelay controller RecoveryDelay, added to the outage
     */
    MetricsTestCase(Time recoveryDelay)
        : TestCase("SRS-T05 metrics for a known outage, RecoveryDelay=" +
                   std::to_string(recoveryDelay.GetMilliSeconds()) + "ms"),
          m_recoveryDelay(recoveryDelay)
    {
    }

  private:
    void DoRun() override
    {
        NodeContainer nodes;
        NetDeviceContainer devices;
        BuildTwoNodeNetwork(nodes, devices);

        const Time failureStart = Seconds(2);
        const Time outage = Seconds(5);
        Ptr<ScheduledAvailabilityModel> model = CreateObject<ScheduledAvailabilityModel>();
        model->AddDownInterval(failureStart, outage);

        ResilienceHelper helper;
        helper.SetControllerAttribute("RecoveryDelay", TimeValue(m_recoveryDelay));
        Ptr<FailureController> controller = helper.Install(devices, model);
        Ptr<ResilienceMetricsCollector> collector = CreateObject<ResilienceMetricsCollector>();
        collector->Monitor(controller);

        controller->TraceConnectWithoutContext("LinkDown",
                                               MakeCallback(&MetricsTestCase::OnLinkDown, this));
        controller->TraceConnectWithoutContext("LinkUp",
                                               MakeCallback(&MetricsTestCase::OnLinkUp, this));
        controller->TraceConnectWithoutContext("RecoveryCompleted",
                                               MakeCallback(&MetricsTestCase::OnRecovery, this));

        Simulator::Stop(Seconds(20));
        Simulator::Run();

        const Time expectedDisruption = outage + m_recoveryDelay;
        NS_TEST_ASSERT_MSG_EQ(collector->GetFailureCount(), 1U, "Failure count");
        NS_TEST_ASSERT_MSG_EQ(collector->GetRecoveryCount(), 1U, "Recovery count");
        NS_TEST_ASSERT_MSG_EQ(collector->GetTotalDisruptionTime(),
                              expectedDisruption,
                              "Total disruption");
        NS_TEST_ASSERT_MSG_EQ(collector->GetLastRecoveryTime(),
                              failureStart + expectedDisruption,
                              "Last recovery time");
        NS_TEST_ASSERT_MSG_EQ(m_linkDownCount, 2U, "LinkDown once per interface");
        NS_TEST_ASSERT_MSG_EQ(m_linkUpCount, 2U, "LinkUp once per interface");
        NS_TEST_ASSERT_MSG_EQ(m_lastDisruption, expectedDisruption, "RecoveryCompleted value");
        Simulator::Destroy();
    }

    /**
     * LinkDown trace sink.
     * @param nodeId node id (unused)
     * @param interfaceId interface index (unused)
     */
    void OnLinkDown(uint32_t /* nodeId */, uint32_t /* interfaceId */)
    {
        ++m_linkDownCount;
    }

    /**
     * LinkUp trace sink.
     * @param nodeId node id (unused)
     * @param interfaceId interface index (unused)
     */
    void OnLinkUp(uint32_t /* nodeId */, uint32_t /* interfaceId */)
    {
        ++m_linkUpCount;
    }

    /**
     * RecoveryCompleted trace sink.
     * @param nodeId node id (unused)
     * @param interfaceId interface index (unused)
     * @param disruption reported outage length
     */
    void OnRecovery(uint32_t /* nodeId */, uint32_t /* interfaceId */, Time disruption)
    {
        m_lastDisruption = disruption;
    }

    Time m_recoveryDelay;        //!< RecoveryDelay under test
    uint32_t m_linkDownCount{0}; //!< LinkDown calls
    uint32_t m_linkUpCount{0};   //!< LinkUp calls
    Time m_lastDisruption;       //!< last RecoveryCompleted value
};

/**
 * @ingroup futurenet-resilience
 * FR-09: InstallOnNode fails every interface of one node, and only those.
 */
class NodeFailureTestCase : public TestCase
{
  public:
    NodeFailureTestCase()
        : TestCase("FR-09 node-wide failure")
    {
    }

  private:
    void DoRun() override
    {
        // Chain n0 - n1 - n2 on two separate channels.
        m_nodes.Create(3);
        SimpleNetDeviceHelper simple;
        NetDeviceContainer left = simple.Install(NodeContainer(m_nodes.Get(0), m_nodes.Get(1)));
        NetDeviceContainer right = simple.Install(NodeContainer(m_nodes.Get(1), m_nodes.Get(2)));
        InternetStackHelper internet;
        internet.Install(m_nodes);
        Ipv4AddressHelper address("10.0.1.0", "255.255.255.0");
        address.Assign(left);
        address.SetBase("10.0.2.0", "255.255.255.0");
        address.Assign(right);

        Ptr<ScheduledAvailabilityModel> model = CreateObject<ScheduledAvailabilityModel>();
        model->AddDownInterval(Seconds(1), Seconds(2));
        ResilienceHelper helper;
        Ptr<FailureController> controller = helper.InstallOnNode(m_nodes.Get(1), model);
        NS_TEST_ASSERT_MSG_EQ(controller->GetNManagedInterfaces(), 2U, "Both n1 interfaces");

        Simulator::Schedule(Seconds(2), &NodeFailureTestCase::CheckDuringOutage, this);
        Simulator::Stop(Seconds(4));
        Simulator::Run();
        NS_TEST_ASSERT_MSG_EQ(IsInterfaceUp(m_nodes.Get(1), 1), true, "n1 if1 recovered");
        NS_TEST_ASSERT_MSG_EQ(IsInterfaceUp(m_nodes.Get(1), 2), true, "n1 if2 recovered");
        Simulator::Destroy();
    }

    /// Check interface states in the middle of the outage.
    void CheckDuringOutage()
    {
        NS_TEST_EXPECT_MSG_EQ(IsInterfaceUp(m_nodes.Get(1), 1), false, "n1 if1 still up");
        NS_TEST_EXPECT_MSG_EQ(IsInterfaceUp(m_nodes.Get(1), 2), false, "n1 if2 still up");
        NS_TEST_EXPECT_MSG_EQ(IsInterfaceUp(m_nodes.Get(0), 1), true, "n0 was failed");
        NS_TEST_EXPECT_MSG_EQ(IsInterfaceUp(m_nodes.Get(2), 1), true, "n2 was failed");
    }

    NodeContainer m_nodes; //!< n0, n1, n2
};

/**
 * @ingroup futurenet-resilience
 * Test suite for the futurenet-resilience module.
 */
class FuturenetResilienceTestSuite : public TestSuite
{
  public:
    FuturenetResilienceTestSuite()
        : TestSuite("futurenet-resilience", Type::UNIT)
    {
        AddTestCase(new DefaultValuesTestCase, TestCase::Duration::QUICK);
        AddTestCase(new AttributeValidationTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ScheduledTimesTestCase, TestCase::Duration::QUICK);
        AddTestCase(new InterfaceStateTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ReproducibilityTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ZeroFailureProbabilityTestCase, TestCase::Duration::QUICK);
        AddTestCase(new MetricsTestCase(Seconds(0)), TestCase::Duration::QUICK);
        AddTestCase(new MetricsTestCase(Seconds(1)), TestCase::Duration::QUICK);
        AddTestCase(new NodeFailureTestCase, TestCase::Duration::QUICK);
    }
};

/// Static instance registering the suite with the test framework.
static FuturenetResilienceTestSuite g_futurenetResilienceTestSuite;
