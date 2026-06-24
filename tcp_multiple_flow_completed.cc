/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <fstream>
#include <map>
#include <sstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-header.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TCPMultiFlow");

class MyApp : public Application {
public:
    MyApp();
    virtual ~MyApp();
    void Setup(Ptr<Socket> socket, Address address, uint32_t packetSize,
               uint32_t nPackets, DataRate dataRate);
private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    void ScheduleTx(void);
    void SendPacket(void);

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    uint32_t m_nPackets;
    DataRate m_dataRate;
    EventId m_sendEvent;
    bool m_running;
    uint32_t m_packetsSent;
};

MyApp::MyApp()
    : m_socket(0), m_peer(), m_packetSize(0), m_nPackets(0),
      m_dataRate(0), m_sendEvent(), m_running(false), m_packetsSent(0) {}

MyApp::~MyApp() { m_socket = 0; }

void MyApp::Setup(Ptr<Socket> socket, Address address, uint32_t packetSize,
                  uint32_t nPackets, DataRate dataRate) {
    m_socket = socket;
    m_peer = address;
    m_packetSize = packetSize;
    m_nPackets = nPackets;
    m_dataRate = dataRate;
}

void MyApp::StartApplication(void) {
    m_running = true;
    m_packetsSent = 0;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    SendPacket();
}

void MyApp::StopApplication(void) {
    m_running = false;
    if (m_sendEvent.IsRunning()) {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_socket) {
        m_socket->Close();
    }
}

void MyApp::SendPacket(void) {
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    if (m_nPackets == 0 || (++m_packetsSent < m_nPackets)) {
        ScheduleTx();
    }
}

void MyApp::ScheduleTx(void) {
    if (m_running) {
        Time tNext(Seconds(m_packetSize * 8 / static_cast<double>(m_dataRate.GetBitRate())));
        m_sendEvent = Simulator::Schedule(tNext, &MyApp::SendPacket, this);
    }
}

static void CwndChange(Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd) {
    *stream->GetStream() << Simulator::Now().GetSeconds() << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void ChangeDelay(std::string newDelay, std::string channelId) {
    std::cout << Simulator::Now().GetSeconds() << " Setting new delay on channel " << channelId << std::endl;
    std::string specificChannel = "/ChannelList/" + channelId + "/$ns3::PointToPointChannel/Delay";
    Config::Set(specificChannel, StringValue(newDelay));
}

static void ThroughputMonitor(Ptr<FlowMonitor> monitor,
                              Ptr<Ipv4FlowClassifier> classifier,
                              Ptr<OutputStreamWrapper> stream,
                              std::map<FlowId, uint64_t>* lastRxBytes,
                              double interval,
                              double stopTime) {
    monitor->CheckForLostPackets();
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    double now = Simulator::Now().GetSeconds();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator it = stats.begin(); it != stats.end(); ++it) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(it->first);
        uint64_t previous = 0;
        if (lastRxBytes->find(it->first) != lastRxBytes->end()) {
            previous = (*lastRxBytes)[it->first];
        }
        double mbps = (it->second.rxBytes - previous) * 8.0 / interval / 1000000.0;
        (*lastRxBytes)[it->first] = it->second.rxBytes;
        *stream->GetStream() << now << "\t" << it->first << "\t" << mbps << "\t"
                             << static_cast<uint32_t>(t.protocol) << "\t" << t.sourceAddress << "\t"
                             << t.destinationAddress << "\t" << t.sourcePort << "\t" << t.destinationPort << std::endl;
    }

    if (now + interval <= stopTime) {
        Simulator::Schedule(Seconds(interval), &ThroughputMonitor, monitor, classifier,
                            stream, lastRxBytes, interval, stopTime);
    }
}

int main(int argc, char *argv[]) {
    std::string experiment_name = "both_new_reno";
    uint32_t run = 0;
    std::string tcp_variant = "TcpNewReno";
    bool increase_delay = false;
    bool enable_udp = false;
    double simulation_time = 60.0;

    CommandLine cmd;
    cmd.AddValue("expname", "Experiment name used as basename for trace files.", experiment_name);
    cmd.AddValue("run", "Run index for repeatable seeds.", run);
    cmd.AddValue("tcpVariant", "TCP variant, e.g. TcpNewReno or TcpBic.", tcp_variant);
    cmd.AddValue("increaseDelay", "Set true for Task 6.2.5: receiver-1 access link delay changes to 20ms at t=20s.", increase_delay);
    cmd.AddValue("enableUdp", "Set true for Bonus Task 6.2.6: add 10Mbps UDP flow at t=30s.", enable_udp);
    cmd.AddValue("simTime", "Simulation time in seconds.", simulation_time);
    cmd.Parse(argc, argv);

    SeedManager::SetSeed(1);
    SeedManager::SetRun(run);

    NodeContainer nodes;
    nodes.Create(6);

    NodeContainer tx1s1(nodes.Get(0), nodes.Get(2));
    NodeContainer tx2s1(nodes.Get(1), nodes.Get(2));
    NodeContainer s1s2(nodes.Get(2), nodes.Get(3));
    NodeContainer rx1s2(nodes.Get(3), nodes.Get(4));
    NodeContainer rx2s2(nodes.Get(3), nodes.Get(5));

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("20ms"));
    NetDeviceContainer devices_s1s2 = p2p.Install(s1s2);

    p2p.SetDeviceAttribute("DataRate", StringValue("50Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("5ms"));
    NetDeviceContainer devices_tx1s1 = p2p.Install(tx1s1);
    NetDeviceContainer devices_tx2s1 = p2p.Install(tx2s1);
    NetDeviceContainer devices_rx1s2 = p2p.Install(rx1s2);
    NetDeviceContainer devices_rx2s2 = p2p.Install(rx2s2);

    std::stringstream tcp_variant_fullname;
    tcp_variant_fullname << "ns3::" << tcp_variant;
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(tcp_variant_fullname.str()));

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer if_tx1s1 = ipv4.Assign(devices_tx1s1);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer if_tx2s1 = ipv4.Assign(devices_tx2s1);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer if_s1s2 = ipv4.Assign(devices_s1s2);
    ipv4.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer if_rx1s2 = ipv4.Assign(devices_rx1s2);
    ipv4.SetBase("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer if_rx2s2 = ipv4.Assign(devices_rx2s2);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t sinkPort1 = 8080;
    uint16_t sinkPort2 = 8081;

    Address sinkAddress1(InetSocketAddress(if_rx1s2.GetAddress(1), sinkPort1));
    PacketSinkHelper packetSinkHelper1("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort1));
    ApplicationContainer sinkApps1 = packetSinkHelper1.Install(nodes.Get(4));
    sinkApps1.Start(Seconds(0.0));
    sinkApps1.Stop(Seconds(simulation_time));

    Ptr<Socket> tcpSocket1 = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    Ptr<MyApp> app1 = CreateObject<MyApp>();
    app1->Setup(tcpSocket1, sinkAddress1, 1040, 0, DataRate("20Mbps"));
    nodes.Get(0)->AddApplication(app1);
    app1->SetStartTime(Seconds(1.0));
    app1->SetStopTime(Seconds(simulation_time));

    Address sinkAddress2(InetSocketAddress(if_rx2s2.GetAddress(1), sinkPort2));
    PacketSinkHelper packetSinkHelper2("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort2));
    ApplicationContainer sinkApps2 = packetSinkHelper2.Install(nodes.Get(5));
    sinkApps2.Start(Seconds(0.0));
    sinkApps2.Stop(Seconds(simulation_time));

    Ptr<Socket> tcpSocket2 = Socket::CreateSocket(nodes.Get(1), TcpSocketFactory::GetTypeId());
    Ptr<MyApp> app2 = CreateObject<MyApp>();
    app2->Setup(tcpSocket2, sinkAddress2, 1040, 0, DataRate("20Mbps"));
    nodes.Get(1)->AddApplication(app2);
    app2->SetStartTime(Seconds(11.0));
    app2->SetStopTime(Seconds(simulation_time));

    if (enable_udp) {
        uint16_t udpPort = 9090;
        PacketSinkHelper udpSink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), udpPort));
        ApplicationContainer udpSinkApps = udpSink.Install(nodes.Get(4));
        udpSinkApps.Start(Seconds(0.0));
        udpSinkApps.Stop(Seconds(simulation_time));

        OnOffHelper udpClient("ns3::UdpSocketFactory", InetSocketAddress(if_rx1s2.GetAddress(1), udpPort));
        udpClient.SetAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
        udpClient.SetAttribute("PacketSize", UintegerValue(1040));
        udpClient.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        udpClient.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        ApplicationContainer udpApps = udpClient.Install(nodes.Get(0));
        udpApps.Start(Seconds(30.0));
        udpApps.Stop(Seconds(simulation_time));
    }

    AsciiTraceHelper asciiTraceHelper;
    std::stringstream fname_cwnd1;
    fname_cwnd1 << "output/multi_tcp_" << experiment_name << "_flow1.cwnd";
    Ptr<OutputStreamWrapper> streamCwnd1 = asciiTraceHelper.CreateFileStream(fname_cwnd1.str());
    tcpSocket1->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback(&CwndChange, streamCwnd1));

    std::stringstream fname_cwnd2;
    fname_cwnd2 << "output/multi_tcp_" << experiment_name << "_flow2.cwnd";
    Ptr<OutputStreamWrapper> streamCwnd2 = asciiTraceHelper.CreateFileStream(fname_cwnd2.str());
    tcpSocket2->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback(&CwndChange, streamCwnd2));

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    std::stringstream fname_throughput;
    fname_throughput << "output/multi_tcp_" << experiment_name << ".throughput";
    Ptr<OutputStreamWrapper> streamThroughput = asciiTraceHelper.CreateFileStream(fname_throughput.str());
    *streamThroughput->GetStream() << "time\tflowId\tthroughputMbps\tprotocol\tsrc\tdst\tsrcPort\tdstPort" << std::endl;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, uint64_t> lastRxBytes;
    Simulator::Schedule(Seconds(1.0), &ThroughputMonitor, flowMonitor, classifier,
                        streamThroughput, &lastRxBytes, 1.0, simulation_time);

    if (increase_delay) {
        uint32_t channelId = devices_rx1s2.Get(0)->GetChannel()->GetId();
        Simulator::Schedule(Seconds(20.0), &ChangeDelay, std::string("20ms"), std::to_string(channelId));
    }

    Simulator::Stop(Seconds(simulation_time));
    Simulator::Run();

    std::stringstream fname_flowmon;
    fname_flowmon << "output/multi_tcp_" << experiment_name << ".flowmonitor";
    flowMonitor->SerializeToXmlFile(fname_flowmon.str(), true, true);

    Simulator::Destroy();
    return 0;
}
