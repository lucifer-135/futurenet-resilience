# futurenet-resilience: technical documentation

## 1. Architecture

The module separates *when* a link fails (the policy) from *how* it fails (the mechanism), following guideline 1.3. Metrics are collected only from trace sources.

```
 ScheduledAvailabilityModel ─┐
                             ├─> LinkAvailabilityModel ──callback──> FailureController ──> Ipv4::SetDown/SetUp
 TwoStateAvailabilityModel  ─┘     (decides WHEN)                     (applies state)          │
                                                                          │                    v
                                                          LinkDown / LinkUp /          routing protocol reacts
                                                          RecoveryCompleted traces     (global routing, RIP, ...)
                                                                          │
                                                                          v
                                                          ResilienceMetricsCollector ──> counters + CSV

 ResilienceHelper: creates controllers and models, finds IPv4 interfaces, starts models.
```

## 2. Class relationships

```
Object
 ├── LinkAvailabilityModel           (abstract; InitialState attribute)
 │    ├── ScheduledAvailabilityModel (explicit times)
 │    └── TwoStateAvailabilityModel  (random, discrete-time Markov chain)
 ├── FailureController               has-a LinkAvailabilityModel, manages N (node, interface) pairs
 └── ResilienceMetricsCollector      listens to FailureController traces (any number of controllers)

ResilienceHelper (plain class)       uses ObjectFactory for models and controllers
```

There is one controller per failure unit:

- For a **link**, the unit is both ends of a point-to-point link.
- For a **node**, the unit is every interface of that node.

## 3. Parameters

| Attribute | Class | Default | Range | Unit |
|---|---|---|---|---|
| InitialState | LinkAvailabilityModel | UP | UP / DOWN | – |
| EvaluationInterval | TwoStateAvailabilityModel | 1 s | 10 ms – 60 s | time |
| FailureProbability | TwoStateAvailabilityModel | 0.01 | 0 – 1 | per evaluation |
| RecoveryProbability | TwoStateAvailabilityModel | 0.10 | 0 – 1 | per evaluation |
| RecoveryDelay | FailureController | 0 s | 0 – 300 s | time |
| random stream | TwoStateAvailabilityModel | assigned | `AssignStreams()` | – |

These defaults and ranges are project configuration values (from the SRS), not values taken from a standard.

The SRS also lists some parameters that are not implemented as attributes:

- **FailureMode:** chosen by which model class you use, `ScheduledAvailabilityModel` or `TwoStateAvailabilityModel`. Pass the model to `Install()` or set the type with `ResilienceHelper::SetAvailabilityModel()`.
- **MeanUpTime / MeanDownTime:** these follow from the probabilities:
  `mean up time = EvaluationInterval / FailureProbability` and `mean down time = EvaluationInterval / RecoveryProbability`. The baseline uses only the discrete probabilities, so the two styles are never mixed (SRS section 6).
- **DegradedLossRatio / DegradedDelayAdd:** not implemented, because DEGRADED (FR-08) is Recommended only.

## 4. Algorithms

**ScheduledAvailabilityModel**

```
pending = multimap time -> state            (sorted by time)
Start(): state = InitialState; ScheduleNext()
ScheduleNext(): cancel event; if pending not empty: schedule ApplyNext at pending.first.time
ApplyNext(): take first entry, erase it, SetState(entry.state), ScheduleNext()
```

Only one simulator event exists at a time. This means changes added after `Start()` are still applied in time order.

**TwoStateAvailabilityModel** (two-state Markov chain, discrete time)

```
every EvaluationInterval:
    u = Uniform[0,1)                         (drawn every time -> reproducible)
    if UP   and u < p: SetState(DOWN)
    if DOWN and u < q: SetState(UP)
```

The closed forms are used for validation:

- Steady-state availability is `q / (p + q)`.
- Up and down durations are geometric, with means `T/p` and `T/q`.

**FailureController.ApplyState(state)**

```
DOWN: cancel pending recovery; if already DOWN return
      remember downSince; for each managed interface: Ipv4::SetDown, fire LinkDown
UP:   if UP or recovery already pending return
      after RecoveryDelay: for each interface: Ipv4::SetUp, fire LinkUp,
                           fire RecoveryCompleted(now - downSince)
```

**ResilienceMetricsCollector**

The trace context string carries the link index, so the collector knows which controller fired:

- The first LinkDown of a controller counts as one failure. The other interfaces of the same controller do not add to the count.
- The first RecoveryCompleted counts as one recovery and adds its disruption to the total.
- `GetTotalDisruptionTime()` also adds outages that are still in progress, counted up to now.

The cost is O(1) per event, plus O(links) when `GetTotalDisruptionTime()` is called. There is no per-tick scan.

## 5. Trace sources

| Source | Class | Signature | Fired |
|---|---|---|---|
| LinkDown | FailureController | `(uint32_t nodeId, uint32_t interfaceId)` | once per managed interface, at failure |
| LinkUp | FailureController | `(uint32_t nodeId, uint32_t interfaceId)` | once per managed interface, at recovery |
| RecoveryCompleted | FailureController | `(uint32_t nodeId, uint32_t interfaceId, Time disruption)` | together with LinkUp |

LinkDegraded is not provided, because DEGRADED is not implemented.

## 6. Validation methodology

All runs record the seed and run number in `<prefix>-summary.csv`. Report them together with:

- the ns-3 version (3.46.1),
- the git commit,
- the command line.

`tools/run-validation.sh` runs all the cases below.

| Case | Command (from the ns-3 root) | Expected |
|---|---|---|
| V1 baseline (SRS V1) | `resilience-example-scheduled` | Link disruption = 5.000 s and last recovery = 15 s. The longest traffic gap is only a few packet intervals, because traffic reroutes through n1. |
| V1b no alternate path (SRS V3) | `resilience-example-scheduled --enableBackup=false` | The longest traffic gap is about 5 s, because traffic stops until the link returns. |
| V2 vary one parameter (SRS V2) | `resilience-example-random --recoveryProbability=q` for q = 0.2, 0.1, 0.05 and runs 1–5 | The mean outage grows (expected 5, 10 and 20 s) and availability falls toward `q/(p+q)`. |
| V3 boundary | `resilience-example-random --failureProbability=0` | 0 failures and availability 1. |
| V3 boundary | `resilience-example-random --failureProbability=1 --recoveryProbability=1` | The links flap at every evaluation and availability is about 0.5. |

The automated tests cover T1–T5 and T6. T6 is an integration test with the real `Ipv4` model, in SRS-T02 and FR-09.

### Reference results

These were produced with ns-3.46.1, the `default` build profile, seed 1, and `tools/run-validation.sh`.

| Case | Result |
|---|---|
| V1 with backup | Link disruption 5 s, recovery at 15 s, longest traffic gap 0.0108 s (about one lost packet, then rerouted) |
| V1b no backup | Link disruption 5 s, longest traffic gap 5.008 s |
| V2 q = 0.2 / 0.1 / 0.05 (5 runs each) | Mean outage 4.38 / 8.94 / 17.24 s (expected 5 / 10 / 20 s). Availability 0.958 / 0.919 / 0.853 (expected 0.952 / 0.909 / 0.833) |
| V3 p = 0 | 0 failures, availability 1.000 |
| V3 p = q = 1 | Availability 0.500 (expected 0.500), delivery ratio 0.749 |
| Closed-form check, one 20 000 s run (`--simulationTime=20000s --numFlows=1 --appRate=1kbps`) | 3600 failures, mean outage 10.07 s (expected 10), availability 0.9094 (expected 0.9091) |

The short V2 runs are about 10% below the expected outage in every row. That is sampling noise, not bias: every q value reuses the same streams (runs 1–5), so their errors are correlated. The long run agrees with the closed form to within 1%.

## 7. Extension points

- **New failure model:** subclass `LinkAvailabilityModel`, implement `DoStart()` and `GetNextStateChange()`, and call `SetState()`. Override `AssignStreams()` if the model is random. Examples are trace-driven failures, or correlated/regional failures (FR-10), where one model drives several controllers.
- **External control (M10 Agent):** call `FailureController::ApplyState()` directly, with no model set. The collector reports the cause as `manual`.
- **DEGRADED state (FR-08):** add a `DEGRADED` value to `LinkAvailability`. The controller would then attach a `ReceiveErrorModel` (for example `RateErrorModel` with `DegradedLossRatio`) to the managed devices instead of calling `SetDown`.
- **Routing with convergence delay:** the examples use global routing, which recomputes routes instantly. Replace it with `RipHelper` or `OlsrHelper` to observe real convergence time. The module itself does not change.
