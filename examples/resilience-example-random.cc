/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * example-advanced (SRS "example-random"): random failures on a grid.
 *
 * A rows x cols grid (default 6 x 5 = 30 nodes) of point-to-point links.
 * --numManagedLinks horizontal links (default 20) each get their own
 * TwoStateAvailabilityModel. Vertical links never fail; every sink is
 * addressed on a vertical link, so a sink is reachable whenever a path exists
 * and lost packets mean "no path", not "address down".
 *
 * --numFlows UDP flows cross the grid from the left to the right column.
 * Global routing recomputes routes on every interface event.
 *
 * Output files (prefix set by --outputPrefix):
 *   <prefix>-events.csv   link events from ResilienceMetricsCollector
 *   <prefix>-summary.csv  one row per run, appended (for validation sweeps)
 *
 *   ./ns3 run "resilience-example-random --recoveryProbability=0.05 --RngRun=3"
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/futurenet-resilience-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ResilienceExampleRandom");

/// The grid topology after addresses are assigned.
struct Grid
{
    NodeContainer nodes;                             //!< rows * cols nodes, row by row
    std::vector<NetDeviceContainer> horizontalLinks; //!< links that may fail
    std::vector<Ipv4Address> stableAddressByNodeId;  //!< one vertical-link address per node
};

/**
 * @param row grid row
 * @param col grid column
 * @param cols number of grid columns
 * @return the index in the node container of the node at (row, col)
 */
uint32_t
NodeIndex(uint32_t row, uint32_t col, uint32_t cols)
{
    return row * cols + col;
}

/**
 * Build a rows x cols grid of point-to-point links, install IPv4 with one /30
 * subnet per link, and fill the global routing tables.
 * @param rows number of grid rows
 * @param cols number of grid columns
 * @param linkRate data rate of every link
 * @param linkDelay propagation delay of every link
 * @return the grid
 */
Grid
BuildGrid(uint32_t rows, uint32_t cols, const std::string& linkRate, const std::string& linkDelay)
{
    Grid grid;
    grid.nodes.Create(rows * cols);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(linkRate));
    p2p.SetChannelAttribute("Delay", StringValue(linkDelay));

    std::vector<NetDeviceContainer> verticalLinks;
    for (uint32_t r = 0; r < rows; ++r)
    {
        for (uint32_t c = 0; c + 1 < cols; ++c)
        {
            grid.horizontalLinks.push_back(p2p.Install(grid.nodes.Get(NodeIndex(r, c, cols)),
                                                       grid.nodes.Get(NodeIndex(r, c + 1, cols))));
        }
    }
    for (uint32_t r = 0; r + 1 < rows; ++r)
    {
        for (uint32_t c = 0; c < cols; ++c)
        {
            verticalLinks.push_back(p2p.Install(grid.nodes.Get(NodeIndex(r, c, cols)),
                                                grid.nodes.Get(NodeIndex(r + 1, c, cols))));
        }
    }

    InternetStackHelper internet;
    internet.Install(grid.nodes);

    Ipv4AddressHelper address("10.0.0.0", "255.255.255.252");
    for (const auto& link : grid.horizontalLinks)
    {
        address.Assign(link);
        address.NewNetwork();
    }
    // Vertical links never fail, so an address on one of them stays reachable
    // whenever any path to the node exists.
    grid.stableAddressByNodeId.resize(grid.nodes.GetN());
    for (const auto& link : verticalLinks)
    {
        Ipv4InterfaceContainer interfaces = address.Assign(link);
        address.NewNetwork();
        grid.stableAddressByNodeId[link.Get(0)->GetNode()->GetId()] = interfaces.GetAddress(0);
        grid.stableAddressByNodeId[link.Get(1)->GetNode()->GetId()] = interfaces.GetAddress(1);
    }
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    return grid;
}

/**
 * Install constant-rate UDP flows. Flow f goes from row f, left column, to
 * row rows-1-f, right column, so every flow crosses the grid.
 * @param grid the grid
 * @param rows number of grid rows
 * @param cols number of grid columns
 * @param numFlows number of flows
 * @param appRate sending rate of each flow
 * @param packetSizeBytes UDP payload size
 * @param simulationTime when the sources stop
 */
void
InstallFlows(const Grid& grid,
             uint32_t rows,
             uint32_t cols,
             uint32_t numFlows,
             const std::string& appRate,
             uint32_t packetSizeBytes,
             Time simulationTime)
{
    const uint16_t basePort = 9000;
    OnOffHelper onoff("ns3::UdpSocketFactory", Address());
    onoff.SetConstantRate(DataRate(appRate), packetSizeBytes);
    for (uint32_t f = 0; f < numFlows; ++f)
    {
        Ptr<Node> source = grid.nodes.Get(NodeIndex(f, 0, cols));
        Ptr<Node> sink = grid.nodes.Get(NodeIndex(rows - 1 - f, cols - 1, cols));
        auto port = static_cast<uint16_t>(basePort + f);

        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), port));
        sinkHelper.Install(sink).Start(Seconds(0));

        onoff.SetAttribute(
            "Remote",
            AddressValue(InetSocketAddress(grid.stableAddressByNodeId[sink->GetId()], port)));
        ApplicationContainer sourceApp = onoff.Install(source);
        sourceApp.Start(Seconds(1));
        sourceApp.Stop(simulationTime);
    }
}

/**
 * Give horizontal links, spread evenly over the grid, their own random
 * failure model, and let the collector monitor each of them.
 * @param grid the grid
 * @param numManagedLinks number of links that can fail
 * @param evaluationInterval time between random draws
 * @param failureProbability P(UP -> DOWN) per evaluation
 * @param recoveryProbability P(DOWN -> UP) per evaluation
 * @param firstStream first random stream index
 * @param collector the collector that records the link events
 * @return the controllers, in the order of their CSV link index
 */
std::vector<Ptr<FailureController>>
InstallFailures(const Grid& grid,
                uint32_t numManagedLinks,
                Time evaluationInterval,
                double failureProbability,
                double recoveryProbability,
                int64_t firstStream,
                Ptr<ResilienceMetricsCollector> collector)
{
    ResilienceHelper resilience;
    resilience.SetAvailabilityModel("ns3::TwoStateAvailabilityModel",
                                    "EvaluationInterval",
                                    TimeValue(evaluationInterval),
                                    "FailureProbability",
                                    DoubleValue(failureProbability),
                                    "RecoveryProbability",
                                    DoubleValue(recoveryProbability));

    auto numHorizontalLinks = static_cast<uint32_t>(grid.horizontalLinks.size());
    std::vector<Ptr<FailureController>> controllers;
    for (uint32_t i = 0; i < numManagedLinks; ++i)
    {
        uint32_t linkIndex = i * numHorizontalLinks / numManagedLinks;
        Ptr<FailureController> controller = resilience.Install(grid.horizontalLinks[linkIndex]);
        collector->Monitor(controller);
        controllers.push_back(controller);
    }
    resilience.AssignStreams(controllers, firstStream);
    return controllers;
}

/**
 * @param flowMonitor the flow monitor, after the simulation has run
 * @return received packets divided by sent packets over all flows
 */
double
GetDeliveryRatio(Ptr<FlowMonitor> flowMonitor)
{
    flowMonitor->CheckForLostPackets();
    uint64_t txPackets = 0;
    uint64_t rxPackets = 0;
    for (const auto& flow : flowMonitor->GetFlowStats())
    {
        txPackets += flow.second.txPackets;
        rxPackets += flow.second.rxPackets;
    }
    return txPackets > 0 ? static_cast<double>(rxPackets) / txPackets : 0.0;
}

int
main(int argc, char* argv[])
{
    uint32_t rows = 6;
    uint32_t cols = 5;
    uint32_t numManagedLinks = 20;
    uint32_t numFlows = 3;
    Time simulationTime = Seconds(300);
    Time evaluationInterval = Seconds(1);
    double failureProbability = 0.01;
    double recoveryProbability = 0.10;
    std::string linkRate = "10Mbps";
    std::string linkDelay = "2ms";
    std::string appRate = "200kbps";
    uint32_t packetSizeBytes = 1000;
    int64_t firstStream = 100;
    std::string outputPrefix = "resilience-random";

    CommandLine cmd(__FILE__);
    cmd.AddValue("rows", "Grid rows", rows);
    cmd.AddValue("cols", "Grid columns", cols);
    cmd.AddValue("numManagedLinks", "Horizontal links that can fail", numManagedLinks);
    cmd.AddValue("numFlows", "UDP flows crossing the grid", numFlows);
    cmd.AddValue("simulationTime", "Total simulated time", simulationTime);
    cmd.AddValue("evaluationInterval", "Time between random draws", evaluationInterval);
    cmd.AddValue("failureProbability", "P(UP -> DOWN) per evaluation", failureProbability);
    cmd.AddValue("recoveryProbability", "P(DOWN -> UP) per evaluation", recoveryProbability);
    cmd.AddValue("linkRate", "Data rate of every link", linkRate);
    cmd.AddValue("linkDelay", "Propagation delay of every link", linkDelay);
    cmd.AddValue("appRate", "UDP sending rate per flow", appRate);
    cmd.AddValue("packetSize", "UDP payload size in bytes", packetSizeBytes);
    cmd.AddValue("firstStream", "First random stream index for the failure models", firstStream);
    cmd.AddValue("outputPrefix", "Prefix of the output CSV files", outputPrefix);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(rows < 2 || cols < 2, "The grid needs at least 2 rows and 2 columns");
    const uint32_t numHorizontalLinks = rows * (cols - 1);
    NS_ABORT_MSG_IF(numManagedLinks == 0 || numManagedLinks > numHorizontalLinks,
                    "numManagedLinks must be in [1, " << numHorizontalLinks << "]");
    NS_ABORT_MSG_IF(numFlows == 0 || numFlows > rows, "numFlows must be in [1, rows]");

    Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));

    Grid grid = BuildGrid(rows, cols, linkRate, linkDelay);
    InstallFlows(grid, rows, cols, numFlows, appRate, packetSizeBytes, simulationTime);

    Ptr<ResilienceMetricsCollector> collector = CreateObject<ResilienceMetricsCollector>();
    collector->EnableCsvOutput(outputPrefix + "-events.csv");
    std::vector<Ptr<FailureController>> controllers = InstallFailures(grid,
                                                                      numManagedLinks,
                                                                      evaluationInterval,
                                                                      failureProbability,
                                                                      recoveryProbability,
                                                                      firstStream,
                                                                      collector);

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();

    Simulator::Stop(simulationTime);
    Simulator::Run();

    double deliveryRatio = GetDeliveryRatio(flowMonitor);

    // All links are observed for the whole run, starting UP at time 0.
    double totalDownSeconds = collector->GetTotalDisruptionTime().GetSeconds();
    double observedLinkSeconds = numManagedLinks * simulationTime.GetSeconds();
    double measuredAvailability = 1.0 - totalDownSeconds / observedLinkSeconds;
    uint32_t failures = collector->GetFailureCount();
    // Outages still open at the end of the run count only up to that moment,
    // so in short runs this mean is slightly below the expected T / q.
    double meanOutageSeconds = failures > 0 ? totalDownSeconds / failures : 0.0;

    auto model = DynamicCast<TwoStateAvailabilityModel>(controllers[0]->GetAvailabilityModel());
    double expectedAvailability = model->GetSteadyStateAvailability();
    double expectedMeanOutageSeconds =
        recoveryProbability > 0 ? evaluationInterval.GetSeconds() / recoveryProbability : 0.0;

    std::cout << "Nodes / managed links:     " << grid.nodes.GetN() << " / " << numManagedLinks
              << "\n"
              << "Failures / recoveries:     " << failures << " / "
              << collector->GetRecoveryCount() << "\n"
              << "Total link downtime (s):   " << totalDownSeconds << "\n"
              << "Mean outage (s):           " << meanOutageSeconds << " (expected "
              << expectedMeanOutageSeconds << ")\n"
              << "Availability:              " << measuredAvailability << " (expected "
              << expectedAvailability << ")\n"
              << "Packet delivery ratio:     " << deliveryRatio << "\n";

    // Append one machine-readable row per run; write the header only once.
    std::string summaryFile = outputPrefix + "-summary.csv";
    bool newFile = !std::ifstream(summaryFile).good();
    std::ofstream summary(summaryFile, std::ios::app);
    NS_ABORT_MSG_IF(!summary.is_open(), "Cannot open " << summaryFile);
    if (newFile)
    {
        summary << "rng_seed,rng_run,num_nodes,num_managed_links,simulation_time_s,"
                   "evaluation_interval_s,failure_probability,recovery_probability,failures,"
                   "recoveries,total_down_s,mean_outage_s,expected_mean_outage_s,"
                   "measured_availability,expected_availability,delivery_ratio\n";
    }
    summary << std::fixed << std::setprecision(6) << RngSeedManager::GetSeed() << ","
            << RngSeedManager::GetRun() << "," << grid.nodes.GetN() << "," << numManagedLinks
            << "," << simulationTime.GetSeconds() << "," << evaluationInterval.GetSeconds() << ","
            << failureProbability << "," << recoveryProbability << "," << failures << ","
            << collector->GetRecoveryCount() << "," << totalDownSeconds << ","
            << meanOutageSeconds << "," << expectedMeanOutageSeconds << ","
            << measuredAvailability << "," << expectedAvailability << "," << deliveryRatio
            << "\n";

    Simulator::Destroy();
    return 0;
}
