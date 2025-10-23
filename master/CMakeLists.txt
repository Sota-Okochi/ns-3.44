set(master_sources
    main.cc
    NetSim.cc
    RttForwarderApp.cc
)

build_exec(
  EXECNAME master
  SOURCE_FILES ${master_sources}
  LIBRARIES_TO_LINK ${ns3-libs} ${ns3-contrib-libs}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/master
)
