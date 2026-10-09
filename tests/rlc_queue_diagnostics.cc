// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/nr-rlc-um.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/drop-tail-queue.h"
#include <cassert>
#include <iostream>
using namespace ns3;

class Mac : public NrMacSapProvider
{
  public:
    void TransmitPdu(TransmitPduParameters) override {}
    void BufferStatusReport(BufferStatusReportParameters) override {}
};

int main()
{
    auto queue = CreateObject<DropTailQueue<Packet>>();
    queue->SetMaxSize(QueueSize("1p"));
    assert(queue->Enqueue(Create<Packet>(100)));
    assert(!queue->Enqueue(Create<Packet>(7)));
    assert(queue->GetNPackets() == 1 && queue->GetNBytes() == 100);
    assert(queue->GetTotalDroppedPackets() == 1 && queue->GetTotalDroppedBytes() == 7);
    queue->Dequeue();
    assert(queue->GetNBytes() == 0 && queue->GetTotalDroppedPackets() == 1);
    Mac mac;
    auto rlc = CreateObject<NrRlcUm>();
    rlc->SetNrMacSapProvider(&mac);
    rlc->SetRnti(1);
    rlc->SetLcId(3);
    rlc->SetAttribute("MaxTxBufferSize", UintegerValue(100));
    rlc->SetAttribute("EnablePdcpDiscarding", BooleanValue(false));
    assert(rlc->GetTxBufferBytes() == 0);
    assert(rlc->GetTxBufferHolDelay().IsZero());
    rlc->DoTransmitPdcpPdu(Create<Packet>(100));
    rlc->DoTransmitPdcpPdu(Create<Packet>(7));
    assert(rlc->GetTxBufferBytes() == 100);
    assert(rlc->GetTxBufferEntries() == 1);
    assert(rlc->GetTxOverflowPackets() == 1);
    assert(rlc->GetTxOverflowBytes() == 7);
    Simulator::Schedule(MilliSeconds(10), [rlc]() {
        assert(rlc->GetTxBufferHolDelay() == MilliSeconds(10));
        rlc->DoNotifyTxOpportunity(NrMacSapUser::TxOpportunityParameters(50, 0, 0, 0, 1, 3));
        assert(rlc->GetTxBufferBytes() > 0 && rlc->GetTxBufferBytes() < 100);
        assert(rlc->GetTxBufferHolDelay() == MilliSeconds(10));
        rlc->DoNotifyTxOpportunity(NrMacSapUser::TxOpportunityParameters(200, 0, 0, 0, 1, 3));
        assert(rlc->GetTxBufferBytes() == 0);
        assert(rlc->GetTxBufferEntries() == 0);
        assert(rlc->GetTxBufferHolDelay().IsZero());
        assert(rlc->GetTxOverflowPackets() == 1);
        rlc->Dispose();
    });
    Simulator::Stop(MilliSeconds(11));
    Simulator::Run();
    Simulator::Destroy();
    std::cout << "RLC occupancy, overflow, HOL, fragmentation and empty-buffer checks passed\n";
}
