set(master_sources
    main.cc
    config.cc
    topology.cc
    applications.cc
    handover.cc
    RttForwarderApp.cc
    queue-diagnostics.cc
    ap1-capacity.cc
    capacity-schedule.cc
    ap0-capacity.cc
    ap2-capacity.cc
)

build_exec(
  EXECNAME master
  SOURCE_FILES ${master_sources}
  LIBRARIES_TO_LINK ${ns3-libs} ${ns3-contrib-libs}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/master
)
