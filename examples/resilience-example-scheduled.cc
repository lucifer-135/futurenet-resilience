/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * example-basic (SRS "example-scheduled"): one scheduled outage on a
 * two-path topology.
 *
 *               n1
 *              /  \         backup path n0-n1-n2
 *            n0 -- n2 -- n3
 *             primary     sink
 *
 * UDP traffic flows n0 -> n3. The primary link n0-n2 fails at --failureStart
 * for --failureDuration. Global routing recomputes routes on interface
 * events, so traffic moves to the backup path and back after recovery.
 * With --enableBackup=false the n1 links are not built and traffic simply
 * stops during the outage.
 *
 * Output files (prefix set by --outputPrefix):
 *   <prefix>-events.csv      link events from ResilienceMetricsCollector
 *   <prefix>-throughput.csv  received throughput per sample interval
 *
 *   ./ns3 run "resilience-example-scheduled --failureStart=10s --failureDuration=5s"
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/futurenet-resilience-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <fstream>
#include <iomanip>
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ResilienceExampleScheduled");

/**
 * Samples a PacketSink: writes throughput per interval to CSV and finds the
 * longest gap between two received packets (the traffic-level disruption).
 */
class TrafficObserver
{
  public:
    /**
     * @param sink the sink to observe
     * @param interval throughput sample interval
     * @param fileName throughput CSV file
     */
    TrafficObserver(Ptr<PacketSink> sink, Time interval, const std::string& fileName)
        : m_sink(sink),
          m_interval(interval),
          m_lastRxBytes(0),
          m_lastRxTime(Seconds(0)),
          m_longestGap(Seconds(0)),
          m_longestGapEnd(Seconds(0))
    {
        m_csv.open(fileName);
        NS_ABORT_MSG_IF(!m_csv.is_open(), "Cannot open " << fileName);
        m_csv << std::fixed << std::setprecision(6) << "time_s,throughput_mbps\n";
        m_sink->TraceConnectWithoutContext("Rx", MakeCallback(&TrafficObserver::OnRx, this));
        Simulator::Schedule(m_interval, &TrafficObserver::Sample, this);
    }

    /// @return the longest time between two received packets
    Time GetLongestGap() const
    {
        return m_longestGap;
    }

    /// @return the time the longest gap ended
    Time GetLongestGapEnd() const
    {
        return m_longestGapEnd;
    }

  private:
    /// Write the throughput of the last interval.
    void Sample()
    {
        uint64_t rxBytes = m_sink->GetTotalRx();
        double mbps = (rxBytes - m_lastRxBytes) * 8.0 / m_interval.GetSeconds() / 1e6;
        m_csv << Simulator::Now().GetSeconds() << "," << mbps << "\n";
        m_lastRxBytes = rxBytes;
        Simulator::Schedule(m_interval, &TrafficObserver::Sample, this);
    }

    /// PacketSink Rx trace sink.
    void OnRx(Ptr<const Packet> /* packet */, const Address& /* from */)
    {
        Time now = Simulator::Now();
        if (m_lastRxTime.IsStrictlyPositive() && now - m_lastRxTime > m_longestGap)
        {
            m_longestGap = now - m_lastRxTime;
            m_longestGapEnd = now;
        }
        m_lastRxTime = now;
    }

    Ptr<PacketSink> m_sink; //!< observed sink
    Time m_interval;        //!< sample interval
    uint64_t m_lastRxBytes; //!< bytes received at the previous sample
    Time m_lastRxTime;      //!< time of the previous packet
    Time m_longestGap;      //!< longest gap seen so far
    Time m_longestGapEnd;   //!< when that gap ended
    std::ofstream m_csv;    //!< throughput CSV
};

int
main(int argc, char* argv[])
{
    Time simulationTime = Seconds(30);
    Time failureStart = Seconds(10);
    Time failureDuration = Seconds(5);
    Time recoveryDelay = Seconds(0);
    Time sampleInterval = MilliSeconds(100);
    bool enableBackup = true;
    std::string linkRate = "10Mbps";
    std::string linkDelay = "2ms";
    std::string appRate = "1Mbps";
    uint32_t packetSizeBytes = 1000;
    std::string outputPrefix = "resilience-scheduled";

    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Total simulated time", simulationTime);
    cmd.AddValue("failureStart", "When the primary link fails", failureStart);
    cmd.AddValue("failureDuration", "How long the primary link stays down", failureDuration);
    cmd.AddValue("recoveryDelay", "Extra delay before interfaces come back up", recoveryDelay);
    cmd.AddValue("sampleInterval", "Throughput sample interval", sampleInterval);
    cmd.AddValue("enableBackup", "Build the backup path through n1", enableBackup);
    cmd.AddValue("linkRate", "Data rate of every link", linkRate);
    cmd.AddValue("linkDelay", "Propagation delay of every link", linkDelay);
    cmd.AddValue("appRate", "UDP sending rate", appRate);
    cmd.AddValue("packetSize", "UDP payload size in bytes", packetSizeBytes);
    cmd.AddValue("outputPrefix", "Prefix of the output CSV files", outputPrefix);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(failureStart + failureDuration >= simulationTime,
                    "The outage must end before simulationTime");

    // Recompute global routes whenever an interface goes down or up.
    Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));

    NodeContainer nodes;
    nodes.Create(4);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(linkRate));
    p2p.SetChannelAttribute("Delay", StringValue(linkDelay));

    NetDeviceContainer primaryLink = p2p.Install(nodes.Get(0), nodes.Get(2));
    NetDeviceContainer sinkLink = p2p.Install(nodes.Get(2), nodes.Get(3));
    NetDeviceContainer backupLink1;
    NetDeviceContainer backupLink2;
    if (enableBackup)
    {
        backupLink1 = p2p.Install(nodes.Get(0), nodes.Get(1));
        backupLink2 = p2p.Install(nodes.Get(1), nodes.Get(2));
    }

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(primaryLink);
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer sinkInterfaces = address.Assign(sinkLink);
    if (enableBackup)
    {
        address.SetBase("10.1.3.0", "255.255.255.0");
        address.Assign(backupLink1);
        address.SetBase("10.1.4.0", "255.255.255.0");
        address.Assign(backupLink2);
    }
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Traffic n0 -> n3. The sink address is on the n2-n3 link, which never
    // fails, so n3 stays reachable whenever any path exists.
    const uint16_t port = 9;
    OnOffHelper onoff("ns3::UdpSocketFactory",
                      InetSocketAddress(sinkInterfaces.GetAddress(1), port));
    onoff.SetConstantRate(DataRate(appRate), packetSizeBytes);
    ApplicationContainer sourceApps = onoff.Install(nodes.Get(0));
    sourceApps.Start(Seconds(1));
    sourceApps.Stop(simulationTime);

    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(nodes.Get(3));
    sinkApps.Start(Seconds(0));

    // Failure injection: one scheduled outage of the primary link.
    Ptr<ScheduledAvailabilityModel> schedule = CreateObject<ScheduledAvailabilityModel>();
    schedule->AddDownInterval(failureStart, failureDuration);

    ResilienceHelper resilience;
    resilience.SetControllerAttribute("RecoveryDelay", TimeValue(recoveryDelay));
    Ptr<FailureController> primary = resilience.Install(primaryLink, schedule);

    Ptr<ResilienceMetricsCollector> collector = CreateObject<ResilienceMetricsCollector>();
    collector->EnableCsvOutput(outputPrefix + "-events.csv");
    collector->Monitor(primary);

    TrafficObserver observer(DynamicCast<PacketSink>(sinkApps.Get(0)),
                             sampleInterval,
                             outputPrefix + "-throughput.csv");

    Simulator::Stop(simulationTime);
    Simulator::Run();

    std::cout << "Backup path:             " << (enableBackup ? "yes" : "no") << "\n"
              << "Failures:                " << collector->GetFailureCount() << "\n"
              << "Recoveries:              " << collector->GetRecoveryCount() << "\n"
              << "Link disruption (s):     " << collector->GetTotalDisruptionTime().GetSeconds()
              << "\n"
              << "Last recovery at (s):    " << collector->GetLastRecoveryTime().GetSeconds()
              << "\n"
              << "Longest traffic gap (s): " << observer.GetLongestGap().GetSeconds()
              << " (ended at " << observer.GetLongestGapEnd().GetSeconds() << " s)\n";

    Simulator::Destroy();
    return 0;
}
