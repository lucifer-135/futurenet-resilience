/*
 * Copyright (c) 2026 The LANlords
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef RESILIENCE_METRICS_COLLECTOR_H
#define RESILIENCE_METRICS_COLLECTOR_H

#include "failure-controller.h"

#include "ns3/nstime.h"
#include "ns3/object.h"

#include <fstream>
#include <string>
#include <vector>

namespace ns3
{

/**
 * @ingroup futurenet-resilience
 * @brief Counts failures/recoveries and measures disruption time.
 *
 * Listens only to FailureController trace sources, so it works with any
 * availability model. A failure of a controller that manages several
 * interfaces counts once, not once per interface.
 *
 * Optional CSV log, one row per interface event. It starts with the common
 * FutureNet columns and appends the M04 columns:
 *
 *   time_s,module,event,node_id,peer_id,flow_id,value1,value2,unit,detail,
 *   interface_id,state,cause,disruption_ms
 *
 * value1 repeats disruption_ms (unit "ms"); detail holds the link index.
 */
class ResilienceMetricsCollector : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    ResilienceMetricsCollector();
    ~ResilienceMetricsCollector() override;

    /**
     * @brief Start collecting metrics from @p controller.
     * @param controller the controller to listen to
     */
    void Monitor(Ptr<FailureController> controller);

    /**
     * @brief Write every event to a CSV file (header written immediately).
     * @param fileName path of the CSV file, overwritten if it exists
     */
    void EnableCsvOutput(const std::string& fileName);

    /**
     * @return number of failures (UP -> DOWN transitions of a controller)
     */
    uint32_t GetFailureCount() const;

    /**
     * @return number of completed recoveries (DOWN -> UP transitions)
     */
    uint32_t GetRecoveryCount() const;

    /**
     * @brief Total time links spent DOWN, summed over monitored controllers.
     *
     * Includes outages still in progress, counted up to the current time, so
     * it stays correct when the simulation stops during an outage.
     * @return total disruption time (link-seconds)
     */
    Time GetTotalDisruptionTime() const;

    /**
     * @return time of the most recent recovery, or zero if none happened
     */
    Time GetLastRecoveryTime() const;

  protected:
    void DoDispose() override;

  private:
    /// Per-controller bookkeeping.
    struct MonitoredLink
    {
        Ptr<FailureController> controller; //!< the monitored controller
        bool down;                         //!< true during an outage
        Time downSince;                    //!< start of the current outage
    };

    /**
     * @brief LinkDown trace sink.
     * @param context index of the link in m_links
     * @param nodeId node owning the interface
     * @param interfaceId IPv4 interface index
     */
    void NotifyLinkDown(std::string context, uint32_t nodeId, uint32_t interfaceId);

    /**
     * @brief RecoveryCompleted trace sink.
     * @param context index of the link in m_links
     * @param nodeId node owning the interface
     * @param interfaceId IPv4 interface index
     * @param disruption outage length reported by the controller
     */
    void NotifyRecoveryCompleted(std::string context,
                                 uint32_t nodeId,
                                 uint32_t interfaceId,
                                 Time disruption);

    /**
     * @brief Append one row to the CSV file, if enabled.
     * @param linkIndex index of the link in m_links
     * @param event event name
     * @param nodeId node owning the interface
     * @param interfaceId IPv4 interface index
     * @param state state after the event
     * @param disruption outage length (zero for failures)
     */
    void WriteCsvRow(uint32_t linkIndex,
                     const std::string& event,
                     uint32_t nodeId,
                     uint32_t interfaceId,
                     LinkAvailability state,
                     Time disruption);

    /**
     * @param link a monitored link
     * @return short name of the model that drives it, or "manual"
     */
    static std::string GetCause(const MonitoredLink& link);

    std::vector<MonitoredLink> m_links; //!< monitored controllers
    uint32_t m_failureCount;            //!< failures seen so far
    uint32_t m_recoveryCount;           //!< recoveries seen so far
    Time m_completedDisruption;         //!< sum of finished outages
    Time m_lastRecoveryTime;            //!< time of the last recovery
    std::ofstream m_csv;                //!< CSV output, if enabled
};

} // namespace ns3

#endif /* RESILIENCE_METRICS_COLLECTOR_H */
