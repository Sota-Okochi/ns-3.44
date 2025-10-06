set(master_sources
    /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/master/main.cc
    /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/master/NetSim.cc
)

build_exec(
  EXECNAME master
  SOURCE_FILES ${master_sources}
  LIBRARIES_TO_LINK ${ns3-libs} ${ns3-contrib-libs}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/master
)
