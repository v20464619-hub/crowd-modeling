#!/bin/bash
set -e
./ns3 run 'scratch/tcp_multiple_flow --expname=newreno_base --tcpVariant=TcpNewReno --simTime=60'
./ns3 run 'scratch/tcp_multiple_flow --expname=bic_base --tcpVariant=TcpBic --simTime=60'
./ns3 run 'scratch/tcp_multiple_flow --expname=newreno_delaychange --tcpVariant=TcpNewReno --increaseDelay=true --simTime=60'
./ns3 run 'scratch/tcp_multiple_flow --expname=bic_delaychange --tcpVariant=TcpBic --increaseDelay=true --simTime=60'
./ns3 run 'scratch/tcp_multiple_flow --expname=newreno_udp --tcpVariant=TcpNewReno --enableUdp=true --simTime=60'
./ns3 run 'scratch/tcp_multiple_flow --expname=bic_udp --tcpVariant=TcpBic --enableUdp=true --simTime=60'
