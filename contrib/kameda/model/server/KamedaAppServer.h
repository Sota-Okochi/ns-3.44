/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef KAMEDA_APP_SERVER_H
#define KAMEDA_APP_SERVER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "ns3/APselection.h"

#include <functional>

namespace ns3 {

class KamedaAppServer : public Application{

public:
    explicit KamedaAppServer(const ApSelectionInput& input);
    virtual ~KamedaAppServer();
    void ConfigureCycles(uint32_t count, Time duration);
    void SetMonitorStopOffset(Time offset);
    void SetCycleEndOffset(Time offset);
    void SetCycleEndGuard(Time guard);
    void SetHandoverCallback(const std::function<void(const std::vector<int>&)>& cb);
    void SetTerminalTp(int termIdx, double tpBps);

protected:
    virtual void DoDispose();

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    void HandleRead(Ptr<Socket> socket);
    void HandleAccept(Ptr<Socket> socket, const Address& from);
    void HandleClose(Ptr<Socket> socket);
    void HandleError(Ptr<Socket> socket);

    uint16_t m_listeningPort;
    Ptr<Socket> m_socket;
    std::list<Ptr<Socket> > m_socketList;

    void SERVER_LOG_INFO(std::string info);

    bool m_file_out;
    Ptr<APselection> apselect;
    ApSelectionInput m_input;
    void Ending();
    void ScheduleCycleEnd(uint32_t cycleIndex);
    
    // RTTデータ出力関連の関数
    void WriteRttDataJSON(std::string senderIpAddress, std::string recvMessage);
    void FinalizeJSONFile();
    std::vector<std::string> SplitString(const std::string &input, const std::string &delimiter);
    
    // 監視端末データ処理関数
    void ProcessMonitorData(std::string senderIpAddress, std::string recvMessage);

    uint32_t m_cycleIndex = 0;
    uint32_t m_cycleCount = 1;
    Time m_cycleDuration;
    Time m_monitorStopOffset;
    Time m_cycleEndOffset;
    Time m_cycleEndGuard;
    std::function<void(const std::vector<int>&)> m_handoverCallback;
};

}

#endif /* KAMEDA_APP_SERVER_H */
