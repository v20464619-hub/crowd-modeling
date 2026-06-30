//
// RIP simulation for NS-3 assignment 7.
//
#include <json/json.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#if __has_include("ns3/ping-helper.h")
#include "ns3/ping-helper.h"
#define ASSIGNMENT_USE_PING_HELPER 1
#else
#include "ns3/v4ping-helper.h"
#define ASSIGNMENT_USE_PING_HELPER 0
#endif

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Assignment7Rip");

namespace
{

std::ofstream g_pingRttStream;

std::string
EdgeKey(const std::string& u, const std::string& v)
{
    return u + v;
}

std::string
JoinPath(const std::string& left, const std::string& right)
{
    if (left.empty())
    {
        return right;
    }
    if (left.back() == '/')
    {
        return left + right;
    }
    return left + "/" + right;
}

void
EnsureDirectory(const std::string& path)
{
    if (path.empty())
    {
        return;
    }

    std::string current;
    if (path.front() == '/')
    {
        current = "/";
    }

    std::stringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '/'))
    {
        if (part.empty())
        {
            continue;
        }
        if (!current.empty() && current.back() != '/')
        {
            current += "/";
        }
        current += part;
        if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
        {
            NS_FATAL_ERROR("Could not create output directory '" << current << "'");
        }
    }
}

Json::Value
ReadJson(const std::string& path)
{
    std::ifstream configFile(path, std::ifstream::binary);
    if (!configFile.is_open())
    {
        NS_FATAL_ERROR("Could not open JSON configuration file: " << path);
    }

    Json::Value root;
    configFile >> root;
    return root;
}

bool
FileExists(const std::string& path)
{
    std::ifstream file(path);
    return file.good();
}

std::string
ResolveConfigPath(const std::string& requestedPath)
{
    if (FileExists(requestedPath))
    {
        return requestedPath;
    }
    if (FileExists("topology.json"))
    {
        return "topology.json";
    }
    if (FileExists("/usr/ns3/ns-3.42/scratch/topology.json"))
    {
        return "/usr/ns3/ns-3.42/scratch/topology.json";
    }
    return requestedPath;
}

double
ParseMilliseconds(const std::string& delay)
{
    std::string number;
    for (char c : delay)
    {
        if ((c >= '0' && c <= '9') || c == '.')
        {
            number += c;
        }
    }
    return number.empty() ? 1.0 : std::stod(number);
}

uint16_t
DelayMetric(const std::string& delay)
{
    return static_cast<uint16_t>(std::max(1.0, std::ceil(ParseMilliseconds(delay))));
}

uint64_t
SumDropped(const FlowMonitor::FlowStats& stats)
{
    return std::accumulate(stats.packetsDropped.begin(), stats.packetsDropped.end(), uint64_t{0});
}

#if ASSIGNMENT_USE_PING_HELPER
void
TracePingRtt(std::string context, uint16_t seq, Time rtt)
{
    g_pingRttStream << Simulator::Now().GetSeconds() << "," << seq << "," << rtt.GetSeconds()
                    << ",\"" << context << "\"" << std::endl;
}
#else
void
TracePingRtt(std::string context, Time rtt)
{
    g_pingRttStream << Simulator::Now().GetSeconds() << ",," << rtt.GetSeconds() << ",\"" << context
                    << "\"" << std::endl;
}
#endif

void
WriteFlowStatsSnapshot(Ptr<FlowMonitor> monitor,
                       const std::string& csvPath,
                       double interval,
                       double stopTime)
{
    monitor->CheckForLostPackets();
    std::ofstream csv(csvPath, std::ios::app);
    for (const auto& item : monitor->GetFlowStats())
    {
        const FlowMonitor::FlowStats& stats = item.second;
        csv << Simulator::Now().GetSeconds() << "," << item.first << "," << stats.txPackets << ","
            << stats.rxPackets << "," << stats.lostPackets << "," << SumDropped(stats) << ","
            << stats.delaySum.GetSeconds() << "," << stats.jitterSum.GetSeconds() << std::endl;
    }

    if (Simulator::Now().GetSeconds() + interval <= stopTime)
    {
        Simulator::Schedule(Seconds(interval),
                            &WriteFlowStatsSnapshot,
                            monitor,
                            csvPath,
                            interval,
                            stopTime);
    }
}

void
SetInterfaceState(Ptr<Node> node, uint32_t interface, bool up)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (up)
    {
        ipv4->SetUp(interface);
    }
    else
    {
        ipv4->SetDown(interface);
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    const std::string routerIdentifier = "router";
    const std::string hostIdentifier = "host";
    std::string configPath = "scratch/topology.json";
    std::string outputDir = "output/data/rip";
    double monitorInterval = 1.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("config", "Path to the topology JSON file", configPath);
    cmd.AddValue("outputDir", "Directory for generated data files", outputDir);
    cmd.AddValue("monitorInterval", "Flow monitor snapshot interval in seconds", monitorInterval);
    cmd.Parse(argc, argv);

    EnsureDirectory(outputDir);

    Config::SetDefault("ns3::OnOffApplication::PacketSize", UintegerValue(210));
    Config::SetDefault("ns3::OnOffApplication::DataRate", StringValue("448kb/s"));

    configPath = ResolveConfigPath(configPath);
    Json::Value root = ReadJson(configPath);
    Json::Value jNodes = root["topo"]["nodes"];
    Json::Value jLinks = root["topo"]["links"];
    Json::Value jFlows = root["flows"];
    Json::Value jFails = root["failures"];

    NS_LOG_INFO("Create nodes.");
    NodeContainer allNodes;
    NodeContainer routerNodes;
    NodeContainer hostNodes;
    std::unordered_map<std::string, Ptr<Node>> nodeMap;
    std::unordered_map<std::string, Ptr<Node>> routers;
    std::unordered_map<std::string, Ptr<Node>> hosts;
    for (Json::ArrayIndex i = 0; i < jNodes.size(); ++i)
    {
        std::string id = jNodes[i]["id"].asString();
        std::string type = jNodes[i]["type"].asString();
        Ptr<Node> node = CreateObject<Node>();
        allNodes.Add(node);
        nodeMap.emplace(id, node);

        if (type == routerIdentifier)
        {
            routerNodes.Add(node);
            routers.emplace(id, node);
        }
        else if (type == hostIdentifier)
        {
            hostNodes.Add(node);
            hosts.emplace(id, node);
        }
    }

    std::unordered_map<std::string, NodeContainer> edges;
    for (Json::ArrayIndex i = 0; i < jLinks.size(); ++i)
    {
        std::string u = jLinks[i]["source"].asString();
        std::string v = jLinks[i]["target"].asString();
        NodeContainer edge;
        edge.Add(nodeMap.at(u));
        edge.Add(nodeMap.at(v));
        edges.emplace(EdgeKey(u, v), edge);
    }

    PointToPointHelper p2p;
    std::unordered_map<std::string, NetDeviceContainer> deviceMap;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> ifaceNums;
    for (Json::ArrayIndex i = 0; i < jLinks.size(); ++i)
    {
        std::string u = jLinks[i]["source"].asString();
        std::string v = jLinks[i]["target"].asString();
        std::string key = EdgeKey(u, v);

        p2p.SetDeviceAttribute("DataRate", StringValue(jLinks[i]["DataRate"].asString()));
        p2p.SetChannelAttribute("Delay", StringValue(jLinks[i]["Delay"].asString()));
        deviceMap.emplace(key, p2p.Install(edges.at(key)));

        uint32_t uIf = ifaceNums[u].size() + 1;
        uint32_t vIf = ifaceNums[v].size() + 1;
        ifaceNums[u].emplace(v, uIf);
        ifaceNums[v].emplace(u, vIf);
    }

    RipHelper ripRouting;
    for (const auto& item : hosts)
    {
        const std::string& hostId = item.first;
        for (const auto& neighbor : ifaceNums.at(hostId))
        {
            ripRouting.ExcludeInterface(item.second, neighbor.second);
        }
    }

    for (Json::ArrayIndex i = 0; i < jLinks.size(); ++i)
    {
        std::string u = jLinks[i]["source"].asString();
        std::string v = jLinks[i]["target"].asString();
        uint16_t metric = DelayMetric(jLinks[i]["Delay"].asString());
        if (routers.find(u) != routers.end())
        {
            ripRouting.SetInterfaceMetric(nodeMap.at(u), ifaceNums.at(u).at(v), metric);
        }
        if (routers.find(v) != routers.end())
        {
            ripRouting.SetInterfaceMetric(nodeMap.at(v), ifaceNums.at(v).at(u), metric);
        }
    }

    Ipv4ListRoutingHelper listRH;
    listRH.Add(ripRouting, 0);

    InternetStackHelper routerInternet;
    routerInternet.SetRoutingHelper(listRH);
    routerInternet.Install(routerNodes);

    InternetStackHelper hostInternet;
    hostInternet.Install(hostNodes);

    Ipv4AddressHelper ipv4;
    std::unordered_map<std::string, Ipv4InterfaceContainer> outIface;
    std::unordered_map<std::string, Ipv4Address> gateways;
    std::unordered_map<std::string, Ipv4Address> hostAddresses;
    for (Json::ArrayIndex i = 0; i < jLinks.size(); ++i)
    {
        std::string u = jLinks[i]["source"].asString();
        std::string v = jLinks[i]["target"].asString();
        std::string key = EdgeKey(u, v);
        std::string network = jLinks[i]["Network"].asString();
        uint16_t metric = DelayMetric(jLinks[i]["Delay"].asString());

        ipv4.SetBase(Ipv4Address(network.c_str()), Ipv4Mask("255.255.255.0"));
        Ipv4InterfaceContainer interfaces = ipv4.Assign(deviceMap.at(key));
        outIface.emplace(key, interfaces);

        nodeMap.at(u)->GetObject<Ipv4>()->SetMetric(ifaceNums.at(u).at(v), metric);
        nodeMap.at(v)->GetObject<Ipv4>()->SetMetric(ifaceNums.at(v).at(u), metric);

        if (hosts.find(u) != hosts.end())
        {
            hostAddresses.emplace(u, interfaces.GetAddress(0));
            gateways.emplace(u, interfaces.GetAddress(1));
        }
        if (hosts.find(v) != hosts.end())
        {
            hostAddresses.emplace(v, interfaces.GetAddress(1));
            gateways.emplace(v, interfaces.GetAddress(0));
        }
    }

    Ipv4StaticRoutingHelper staticRouting;
    for (const auto& item : gateways)
    {
        const std::string& hostId = item.first;
        std::string routerId;
        for (const auto& neighbor : ifaceNums.at(hostId))
        {
            routerId = neighbor.first;
            break;
        }
        Ptr<Ipv4StaticRouting> hostStaticRouting =
            staticRouting.GetStaticRouting(nodeMap.at(hostId)->GetObject<Ipv4>());
        hostStaticRouting->SetDefaultRoute(item.second, ifaceNums.at(hostId).at(routerId));
    }

    g_pingRttStream.open(JoinPath(outputDir, "ping-rtt.csv"));
    g_pingRttStream << "time,seq,rtt,context" << std::endl;

    double stopTime = 100.0;
    NS_LOG_INFO("Create applications.");
    for (Json::ArrayIndex i = 0; i < jFlows.size(); ++i)
    {
        std::string type = jFlows[i]["Type"].asString();
        std::string u = jFlows[i]["Source"].asString();
        std::string v = jFlows[i]["Sink"].asString();
        double start = jFlows[i]["StartTime"].asDouble();
        double stop = jFlows[i]["StopTime"].asDouble();
        stopTime = std::max(stopTime, stop + 1.0);

        if (type == "OnOff")
        {
            uint16_t port = static_cast<uint16_t>(jFlows[i]["DstPort"].asUInt());
            PacketSinkHelper sink("ns3::UdpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApps = sink.Install(nodeMap.at(v));
            sinkApps.Start(Seconds(0.0));
            sinkApps.Stop(Seconds(stopTime));

            OnOffHelper onoff("ns3::UdpSocketFactory",
                              InetSocketAddress(hostAddresses.at(v), port));
            onoff.SetAttribute("DataRate", StringValue(jFlows[i]["DataRate"].asString()));
            ApplicationContainer sourceApps = onoff.Install(nodeMap.at(u));
            sourceApps.Start(Seconds(start));
            sourceApps.Stop(Seconds(stop));
        }
        else if (type == "Ping")
        {
#if ASSIGNMENT_USE_PING_HELPER
            PingHelper ping(hostAddresses.at(v));
            ping.SetAttribute("VerboseMode", EnumValue(Ping::SILENT));
#else
            V4PingHelper ping(hostAddresses.at(v));
            ping.SetAttribute("Verbose", BooleanValue(false));
#endif
            ping.SetAttribute("Interval", TimeValue(Seconds(jFlows[i]["Interval"].asDouble())));
            ping.SetAttribute("Size", UintegerValue(jFlows[i]["Size"].asUInt()));
            ApplicationContainer pingApps = ping.Install(nodeMap.at(u));
            pingApps.Start(Seconds(start));
            pingApps.Stop(Seconds(stop));
        }
    }

#if ASSIGNMENT_USE_PING_HELPER
    Config::ConnectFailSafe("/NodeList/*/ApplicationList/*/$ns3::Ping/Rtt",
                            MakeCallback(&TracePingRtt));
#else
    Config::ConnectFailSafe("/NodeList/*/ApplicationList/*/$ns3::V4Ping/Rtt",
                            MakeCallback(&TracePingRtt));
#endif

    for (Json::ArrayIndex i = 0; i < jFails.size(); ++i)
    {
        std::string u = jFails[i]["source"].asString();
        std::string v = jFails[i]["target"].asString();
        double start = jFails[i]["StartTime"].asDouble();
        double stop = jFails[i]["StopTime"].asDouble();
        stopTime = std::max(stopTime, stop + 1.0);

        Simulator::Schedule(Seconds(start),
                            &SetInterfaceState,
                            nodeMap.at(u),
                            ifaceNums.at(u).at(v),
                            false);
        Simulator::Schedule(Seconds(start),
                            &SetInterfaceState,
                            nodeMap.at(v),
                            ifaceNums.at(v).at(u),
                            false);
        Simulator::Schedule(Seconds(stop),
                            &SetInterfaceState,
                            nodeMap.at(u),
                            ifaceNums.at(u).at(v),
                            true);
        Simulator::Schedule(Seconds(stop),
                            &SetInterfaceState,
                            nodeMap.at(v),
                            ifaceNums.at(v).at(u),
                            true);
    }

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();
    std::string statsCsv = JoinPath(outputDir, "flow-stats-timeseries.csv");
    {
        std::ofstream csv(statsCsv);
        csv << "time,flowId,txPackets,rxPackets,lostPackets,droppedPackets,delaySum,jitterSum"
            << std::endl;
    }
    Simulator::Schedule(Seconds(monitorInterval),
                        &WriteFlowStatsSnapshot,
                        monitor,
                        statsCsv,
                        monitorInterval,
                        stopTime);

    RipHelper routingHelper;
    AsciiTraceHelper asciiTraceHelper;
    for (const auto& item : nodeMap)
    {
        Ptr<OutputStreamWrapper> stream =
            asciiTraceHelper.CreateFileStream(JoinPath(outputDir, "routes-" + item.first + ".txt"));
        routingHelper.PrintRoutingTableEvery(Seconds(1), item.second, stream);
    }

    NS_LOG_INFO("Run simulation.");
    Simulator::Stop(Seconds(stopTime));
    Simulator::Run();
    monitor->CheckForLostPackets();
    monitor->SerializeToXmlFile(JoinPath(outputDir, "flowmon.xml"), true, true);
    NS_LOG_INFO("Done.");

    g_pingRttStream.close();
    Simulator::Destroy();
    return 0;
}
