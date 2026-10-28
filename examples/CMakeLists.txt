build_lib_example(
  NAME resilience-example-scheduled
  SOURCE_FILES resilience-example-scheduled.cc
  LIBRARIES_TO_LINK
    ${libfuturenet-resilience}
    ${libpoint-to-point}
    ${libapplications}
    ${libinternet}
)

build_lib_example(
  NAME resilience-example-random
  SOURCE_FILES resilience-example-random.cc
  LIBRARIES_TO_LINK
    ${libfuturenet-resilience}
    ${libpoint-to-point}
    ${libapplications}
    ${libinternet}
    ${libflow-monitor}
)
