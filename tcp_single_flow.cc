/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <fstream>
#include <sstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TCPSingleFlow");

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
static void RxDrop (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p) {
  *stream->GetStream () << Simulator::Now ().GetSeconds () << std::endl;
}

int main (int argc, char *argv[]) {
  uint32_t run = 0;
  std::string experiment_name = "base";
  std::string tcp_variant = "TcpNewReno";
  std::string delay = "10ms";
  double errorRate = 1e-6;
  double simTime = 30.0;

  CommandLine cmd;
  cmd.AddValue ("expname", "Experiment name used as basename for trace files.", experiment_name);
  cmd.AddValue ("run", "Run index (for setting repeatable seeds)", run);
  cmd.AddValue ("tcp_variant", "TCP variant, e.g. TcpNewReno or TcpVegas", tcp_variant);
  cmd.AddValue ("error_rate", "Bit error rate of the P2P receive error model", errorRate);
  cmd.AddValue ("delay", "P2P link delay, e.g. 10ms or 20ms", delay);
  cmd.AddValue ("sim_time", "Simulation time in seconds", simTime);
  cmd.Parse (argc, argv);

  SeedManager::SetSeed (1);
  SeedManager::SetRun (run);

  std::stringstream tcp_variant_fullname;
  tcp_variant_fullname << "ns3::" << tcp_variant;
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue (tcp_variant_fullname.str ()));

  NodeContainer nodes;
  nodes.Create (2);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue (delay));
  NetDeviceContainer devices = pointToPoint.Install (nodes);

  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
  em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_BIT"));
  em->SetAttribute ("ErrorRate", DoubleValue (errorRate));
  devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("192.168.1.0", "255.255.255.252");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  uint16_t sinkPort = 8080;
  Address sinkAddress (InetSocketAddress (interfaces.GetAddress (1), sinkPort));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (1));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (simTime));

  Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (nodes.Get (0), TcpSocketFactory::GetTypeId ());
  Ptr<MyApp> app = CreateObject<MyApp> ();
  app->Setup (ns3TcpSocket, sinkAddress, 1040, 0, DataRate ("20Mbps"));
  nodes.Get (0)->AddApplication (app);
  app->SetStartTime (Seconds (1.0));
  app->SetStopTime (Seconds (simTime));

  AsciiTraceHelper asciiTraceHelper;
  std::stringstream fname_cwnd;
  fname_cwnd << "output/614_" << experiment_name << ".cwnd";
  Ptr<OutputStreamWrapper> streamCwnd = asciiTraceHelper.CreateFileStream (fname_cwnd.str ());
  ns3TcpSocket->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&CwndChange, streamCwnd));

  std::stringstream fname_rxdrop;
  fname_rxdrop << "output/614_" << experiment_name << ".rxdrop";
  Ptr<OutputStreamWrapper> streamRxDrop = asciiTraceHelper.CreateFileStream (fname_rxdrop.str ());
  devices.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeBoundCallback (&RxDrop, streamRxDrop));

  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
