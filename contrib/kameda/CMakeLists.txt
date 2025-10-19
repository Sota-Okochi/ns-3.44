set(kameda_sources
    model/client/ConnectManager.cc
    model/client/CountRtt.cc
    model/client/KamedaAppClient.cc
    model/client/APMonitorTerminal.cc
    model/server/APselection.cc
    model/server/KamedaAppServer.cc
    helper/kameda-helper.cc
)

set(kameda_headers
    model/client/ConnectManager.h
    model/client/CountRtt.h
    model/client/KamedaAppClient.h
    model/server/APselection.h
    model/client/APMonitorTerminal.h
    model/server/KamedaAppServer.h
    helper/kameda-helper.h
)

set(kameda_test_sources
    test/kameda-test-suite.cc
)

build_lib(
  LIBNAME kameda
  SOURCE_FILES ${kameda_sources}
  HEADER_FILES ${kameda_headers}
  LIBRARIES_TO_LINK ${libcore} ${libnetwork} ${libinternet} ${libinternet-apps} ${libapplications} ${libwifi}
  TEST_SOURCES ${kameda_test_sources}
)
