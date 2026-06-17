/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <fstream>
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
  MyApp (); 
  ~MyApp () override;
  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize,
              uint32_t nPackets, DataRate dataRate);
private:
  void StartApplication () override;
  void StopApplication () override;
  void ScheduleTx ();
  void SendPacket ();
  Ptr<Socket> m_socket;
  Address m_peer;
  uint32_t m_packetSize;
  uint32_t m_nPackets;
  DataRate m_dataRate;
  EventId m_sendEvent;
  bool m_running;
  uint32_t m_packetsSent;
};

MyApp::MyApp ()
  : m_socket (0), m_peer (), m_packetSize (0), m_nPackets (0),
    m_dataRate (0), m_sendEvent (), m_running (false), m_packetsSent (0) {}
MyApp::~MyApp () { m_socket = 0; }
void MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize,
                   uint32_t nPackets, DataRate dataRate) {
  m_socket = socket; m_peer = address; m_packetSize = packetSize;
  m_nPackets = nPackets; m_dataRate = dataRate;
}
void MyApp::StartApplication () {
  m_running = true; m_packetsSent = 0; m_socket->Bind (); m_socket->Connect (m_peer); SendPacket ();
}
void MyApp::StopApplication () {
  m_running = false;
  if (m_sendEvent.IsRunning ()) { Simulator::Cancel (m_sendEvent); }
  if (m_socket) { m_socket->Close (); }
}
void MyApp::SendPacket () {
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);
  if (m_nPackets == 0 || (++m_packetsSent < m_nPackets)) { ScheduleTx (); }
}
void MyApp::ScheduleTx () {
  if (m_running) {
    Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
    m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
  }
}

static void CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd) {
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void ChangeDelay (std::string newDelay, std::string channelId) {
  std::cout << Simulator::Now ().GetSeconds () << " Setting new delay on channel " << channelId << " to " << newDelay << std::endl;
  std::string specificNode = "/ChannelList/" + channelId + "/$ns3::PointToPointChannel/Delay";
  Config::Set (specificNode, StringValue (newDelay));
}

static void ThroughputTrace (Ptr<OutputStreamWrapper> stream, Ptr<PacketSink> sink,
                             uint64_t *lastTotalRx, Time interval) {
  uint64_t cur = sink->GetTotalRx ();
  double throughputMbps = (cur - *lastTotalRx) * 8.0 / interval.GetSeconds () / 1000000.0;
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << throughputMbps << std::endl;
  *lastTotalRx = cur;
  Simulator::Schedule (interval, &ThroughputTrace, stream, sink, lastTotalRx, interval);
}

int main (int argc, char *argv[]) {
  std::string experiment_name = "both_new_reno";
  uint32_t run = 0;
  std::string tcp_variant = "TcpNewReno";
  bool change_delay = false;
  bool enable_udp = false;
  double simTime = 60.0;

  CommandLine cmd;
  cmd.AddValue ("expname", "Experiment name used as basename for trace files.", experiment_name);
  cmd.AddValue ("run", "Run index (for setting repeatable seeds)", run);
  cmd.AddValue ("tcp_variant", "TCP variant, e.g. TcpNewReno or TcpBic", tcp_variant);
  cmd.AddValue ("change_delay", "If true, increase Receiver1 access-link delay to 20ms at t=20s", change_delay);
  cmd.AddValue ("enable_udp", "If true, add a 10Mbps UDP flow from Sender1 to Receiver1 at t=30s", enable_udp);
  cmd.AddValue ("sim_time", "Simulation time in seconds", simTime);
  cmd.Parse (argc, argv);

  SeedManager::SetSeed (1);
  SeedManager::SetRun (run);

  std::stringstream tcp_variant_fullname;
  tcp_variant_fullname << "ns3::" << tcp_variant;
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (tcp_variant_fullname.str ()));

  NodeContainer nodes;
  nodes.Create (6);
  NodeContainer tx1s1 = NodeContainer (nodes.Get (0), nodes.Get (2));
  NodeContainer tx2s1 = NodeContainer (nodes.Get (1), nodes.Get (2));
  NodeContainer s1s2 = NodeContainer (nodes.Get (2), nodes.Get (3));
  NodeContainer rx1s2 = NodeContainer (nodes.Get (3), nodes.Get (4));
  NodeContainer rx2s2 = NodeContainer (nodes.Get (3), nodes.Get (5));

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("20ms"));
  NetDeviceContainer devices_s1s2 = p2p.Install (s1s2);

  p2p.SetDeviceAttribute ("DataRate", StringValue ("50Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("5ms"));
  NetDeviceContainer devices_tx1s1 = p2p.Install (tx1s1);
  NetDeviceContainer devices_tx2s1 = p2p.Install (tx2s1);
  NetDeviceContainer devices_rx1s2 = p2p.Install (rx1s2);
  NetDeviceContainer devices_rx2s2 = p2p.Install (rx2s2);

  InternetStackHelper internet;
  internet.Install (nodes);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer if_tx1s1 = ipv4.Assign (devices_tx1s1);
  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer if_tx2s1 = ipv4.Assign (devices_tx2s1);
  ipv4.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer if_s1s2 = ipv4.Assign (devices_s1s2);
  ipv4.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer if_rx1s2 = ipv4.Assign (devices_rx1s2);
  ipv4.SetBase ("10.1.5.0", "255.255.255.0");
  Ipv4InterfaceContainer if_rx2s2 = ipv4.Assign (devices_rx2s2);
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  uint16_t sinkPort1 = 8080;
  uint16_t sinkPort2 = 8081;
  Address sinkAddress1 (InetSocketAddress (if_rx1s2.GetAddress (1), sinkPort1));
  Address sinkAddress2 (InetSocketAddress (if_rx2s2.GetAddress (1), sinkPort2));

  PacketSinkHelper sinkHelper1 ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort1));
  ApplicationContainer sinkApps1 = sinkHelper1.Install (nodes.Get (4));
  sinkApps1.Start (Seconds (0.0)); sinkApps1.Stop (Seconds (simTime));
  PacketSinkHelper sinkHelper2 ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort2));
  ApplicationContainer sinkApps2 = sinkHelper2.Install (nodes.Get (5));
  sinkApps2.Start (Seconds (0.0)); sinkApps2.Stop (Seconds (simTime));

  Ptr<Socket> tcpSocket1 = Socket::CreateSocket (nodes.Get (0), TcpSocketFactory::GetTypeId ());
  Ptr<MyApp> app1 = CreateObject<MyApp> ();
  app1->Setup (tcpSocket1, sinkAddress1, 1040, 0, DataRate ("20Mbps"));
  nodes.Get (0)->AddApplication (app1);
  app1->SetStartTime (Seconds (1.0)); app1->SetStopTime (Seconds (simTime));

  Ptr<Socket> tcpSocket2 = Socket::CreateSocket (nodes.Get (1), TcpSocketFactory::GetTypeId ());
  Ptr<MyApp> app2 = CreateObject<MyApp> ();
  app2->Setup (tcpSocket2, sinkAddress2, 1040, 0, DataRate ("20Mbps"));
  nodes.Get (1)->AddApplication (app2);
  app2->SetStartTime (Seconds (11.0)); app2->SetStopTime (Seconds (simTime));

  if (enable_udp) {
    uint16_t udpPort = 9000;
    PacketSinkHelper udpSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), udpPort));
    ApplicationContainer udpSink = udpSinkHelper.Install (nodes.Get (4));
    udpSink.Start (Seconds (0.0)); udpSink.Stop (Seconds (simTime));
    OnOffHelper udpClient ("ns3::UdpSocketFactory", InetSocketAddress (if_rx1s2.GetAddress (1), udpPort));
    udpClient.SetAttribute ("DataRate", DataRateValue (DataRate ("10Mbps")));
    udpClient.SetAttribute ("PacketSize", UintegerValue (1040));
    udpClient.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    udpClient.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer udpApp = udpClient.Install (nodes.Get (0));
    udpApp.Start (Seconds (30.0)); udpApp.Stop (Seconds (simTime));
  }

  AsciiTraceHelper ascii;
  std::stringstream f1, f2, t1, t2;
  f1 << "output/623_" << experiment_name << "_flow1.cwnd";
  f2 << "output/623_" << experiment_name << "_flow2.cwnd";
  Ptr<OutputStreamWrapper> streamCwnd1 = ascii.CreateFileStream (f1.str ());
  Ptr<OutputStreamWrapper> streamCwnd2 = ascii.CreateFileStream (f2.str ());
  tcpSocket1->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&CwndChange, streamCwnd1));
  tcpSocket2->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&CwndChange, streamCwnd2));

  t1 << "output/624_" << experiment_name << "_flow1.throughput";
  t2 << "output/624_" << experiment_name << "_flow2.throughput";
  Ptr<OutputStreamWrapper> streamThr1 = ascii.CreateFileStream (t1.str ());
  Ptr<OutputStreamWrapper> streamThr2 = ascii.CreateFileStream (t2.str ());
  uint64_t lastRx1 = 0, lastRx2 = 0;
  Ptr<PacketSink> sink1 = DynamicCast<PacketSink> (sinkApps1.Get (0));
  Ptr<PacketSink> sink2 = DynamicCast<PacketSink> (sinkApps2.Get (0));
  Simulator::Schedule (Seconds (1.0), &ThroughputTrace, streamThr1, sink1, &lastRx1, Seconds (1.0));
  Simulator::Schedule (Seconds (1.0), &ThroughputTrace, streamThr2, sink2, &lastRx2, Seconds (1.0));

  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll ();

  if (change_delay) {
    // Channel ID 3 is rx1s2 because links were installed in order: s1s2=0, tx1s1=1, tx2s1=2, rx1s2=3, rx2s2=4.
    Simulator::Schedule (Seconds (20.0), &ChangeDelay, std::string ("20ms"), std::string ("3"));
  }

  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();

  std::stringstream fname_flowmon;
  fname_flowmon << "output/multi_tcp_" << experiment_name << ".flowmonitor";
  flowMonitor->SerializeToXmlFile (fname_flowmon.str (), true, true);

  Simulator::Destroy ();
  return 0;
}
