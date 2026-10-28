build_lib(
  LIBNAME futurenet-resilience
  SOURCE_FILES
    model/link-availability-model.cc
    model/scheduled-availability-model.cc
    model/two-state-availability-model.cc
    model/failure-controller.cc
    model/resilience-metrics-collector.cc
    helper/resilience-helper.cc
  HEADER_FILES
    model/link-availability-model.h
    model/scheduled-availability-model.h
    model/two-state-availability-model.h
    model/failure-controller.h
    model/resilience-metrics-collector.h
    helper/resilience-helper.h
  LIBRARIES_TO_LINK ${libinternet}
  TEST_SOURCES test/futurenet-resilience-test-suite.cc
)
