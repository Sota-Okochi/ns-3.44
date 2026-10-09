#ifdef NS3_MODULE_COMPILATION 
    error "Do not include ns3 module aggregator headers from other modules these are meant only for end user scripts." 
#endif 
#ifndef NS3_MODULE_KAMEDA
    // Module headers: 
    #include <ns3/ConnectManager.h>
    #include <ns3/CountRtt.h>
    #include <ns3/KamedaAppClient.h>
    #include <ns3/APselection.h>
    #include <ns3/APMonitorTerminal.h>
    #include <ns3/KamedaAppServer.h>
    #include <ns3/kameda-helper.h>
#endif 