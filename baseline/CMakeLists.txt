set(baseline_sources
    /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/baseline/main.cc
    /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/baseline/NetSim.cc
)

build_exec(
  EXECNAME baseline
  SOURCE_FILES ${baseline_sources}
  LIBRARIES_TO_LINK ${ns3-libs} ${ns3-contrib-libs}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/baseline
)
