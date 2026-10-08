// SPDX-License-Identifier: GPL-2.0-only
// Small diagnostic integration test; not a reduced research experiment.
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/internet-module.h"
#include "ns3/research-wall-profiler.h"
#include <iostream>
using namespace ns3;
int main(int argc, char** argv)
{
    auto& profiler = ResearchWallProfiler::Get();
    if (argc > 1) profiler.Start(argv[1], true, 1);
    RngSeedManager::SetSeed(argc > 2 ? std::stoul(argv[2]) : 1001);
    NodeContainer nodes;
    nodes.Create(2);
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    nodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(1, 0, 0));
    auto channel = CreateObject<MultiModelSpectrumChannel>();
    channel->AddPropagationLossModel(CreateObject<LogDistancePropagationLossModel>());
    channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());
    SpectrumWifiPhyHelper phy;
    phy.SetChannel(channel);
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");
    WifiMacHelper mac;
    Ssid ssid("perf-smoke");
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    auto ap = wifi.Install(phy, mac, nodes.Get(0));
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    auto sta = wifi.Install(phy, mac, nodes.Get(1));
    wifi.AssignStreams(ap, 10);
    wifi.AssignStreams(sta, 30);
    InternetStackHelper internet;
    internet.Install(nodes);
    NetDeviceContainer devices;
    devices.Add(ap); devices.Add(sta);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.9.0.0", "255.255.255.0");
    auto ips = addresses.Assign(devices);
    profiler.RegisterDevice(nodes.Get(0)->GetId(), ap.Get(0)->GetIfIndex(), 2, "wifi", "base_station");
    profiler.RegisterDevice(nodes.Get(1)->GetId(), sta.Get(0)->GetIfIndex(), 2, "wifi", "terminal");
    profiler.SelectAp(nodes.Get(1)->GetId(), 2);
    auto sink = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
    sink->Bind(InetSocketAddress(Ipv4Address::GetAny(), 9000));
    auto source = Socket::CreateSocket(nodes.Get(1), UdpSocketFactory::GetTypeId());
    source->Connect(InetSocketAddress(ips.GetAddress(0), 9000));
    Simulator::Schedule(Seconds(0.5), [source] { source->Send(Create<Packet>(100)); });
    Simulator::Schedule(Seconds(0.7), [&profiler, &nodes] {
        profiler.SelectAp(nodes.Get(1)->GetId(), 1); // observation label only
    });
    Simulator::Schedule(Seconds(0.8), [source] { source->Send(Create<Packet>(100)); });
    Simulator::Stop(Seconds(1.0));
    profiler.Boundary("run_start", 0, 0, 0);
    Simulator::Run();
    profiler.Boundary("run_end", 0, Simulator::Now().GetSeconds(), Simulator::GetEventCount());
    uint32_t bytes = 0;
    while (auto packet = sink->Recv()) bytes += packet->GetSize();
    std::cout << "bytes=" << bytes << " events=" << Simulator::GetEventCount() << '\n';
    Simulator::Destroy();
    profiler.Finish();
    return bytes == 200 ? 0 : 1;
}
