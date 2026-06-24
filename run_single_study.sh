#!/bin/bash
set -e
./ns3 run 'scratch/tcp_single_flow --expname=newreno_ber1e-5_delay10ms --tcpVariant=TcpNewReno --errorRate=0.00001 --delay=10ms --simTime=30'
./ns3 run 'scratch/tcp_single_flow --expname=newreno_ber1e-6_delay10ms --tcpVariant=TcpNewReno --errorRate=0.000001 --delay=10ms --simTime=30'
./ns3 run 'scratch/tcp_single_flow --expname=newreno_ber1e-5_delay20ms --tcpVariant=TcpNewReno --errorRate=0.00001 --delay=20ms --simTime=30'
./ns3 run 'scratch/tcp_single_flow --expname=newreno_ber1e-6_delay20ms --tcpVariant=TcpNewReno --errorRate=0.000001 --delay=20ms --simTime=30'
./ns3 run 'scratch/tcp_single_flow --expname=vegas_ber1e-5_delay10ms --tcpVariant=TcpVegas --errorRate=0.00001 --delay=10ms --simTime=30'
./ns3 run 'scratch/tcp_single_flow --expname=vegas_ber1e-6_delay10ms --tcpVariant=TcpVegas --errorRate=0.000001 --delay=10ms --simTime=30'
./ns3 run 'scratch/tcp_single_flow --expname=vegas_ber1e-5_delay20ms --tcpVariant=TcpVegas --errorRate=0.00001 --delay=20ms --simTime=30'
./ns3 run 'scratch/tcp_single_flow --expname=vegas_ber1e-6_delay20ms --tcpVariant=TcpVegas --errorRate=0.000001 --delay=20ms --simTime=30'
