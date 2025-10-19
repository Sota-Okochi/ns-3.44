/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef KAMEDA_APP_SERVER_H
#define KAMEDA_APP_SERVER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"
#include "ns3/APselection.h"

namespace ns3 {

class KamedaAppServer : public Application{

public:
    explicit KamedaAppServer(const ApSelectionInput& input);
    virtual ~KamedaAppServer();

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
    
    // RTTデータの高速出力関連の関数
    void InitializeRttDataFile();
    void WriteRttDataBinary(std::string senderIpAddress, std::string recvMessage);
    void WriteRttDataJSON(std::string senderIpAddress, std::string recvMessage);
    void FinalizeJSONFile();
    void OutputRttStatisticsFromBinary();
    std::vector<std::string> SplitString(const std::string &input, const std::string &delimiter);
    
    // 監視端末データ処理関数
    void ProcessMonitorData(std::string senderIpAddress, std::string recvMessage);

};

}

#endif /* KAMEDA_APP_SERVER_H */
