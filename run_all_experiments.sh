#!/bin/bash
set -e
mkdir -p output

# Task 6.1.4: single-flow grid
for tcp in TcpNewReno TcpVegas; do
  for ber in 0.00001 0.000001; do
    for delay in 10ms 20ms; do
      name="${tcp}_ber_${ber}_delay_${delay}"
      ./ns3 run "scratch/tcp_single_flow --expname=${name} --tcp_variant=${tcp} --error_rate=${ber} --delay=${delay} --sim_time=30"
    done
  done
done

# Task 6.2.3: equal-condition multi-flow study
for tcp in TcpBic TcpNewReno; do
  ./ns3 run "scratch/tcp_multiple_flow --expname=${tcp}_equal --tcp_variant=${tcp} --change_delay=false --enable_udp=false --sim_time=60"
done

# Task 6.2.5: change Receiver 1 delay at 20 s
for tcp in TcpBic TcpNewReno; do
  ./ns3 run "scratch/tcp_multiple_flow --expname=${tcp}_delaychange --tcp_variant=${tcp} --change_delay=true --enable_udp=false --sim_time=60"
done

# Bonus Task 6.2.6: add UDP flow at 30 s
for tcp in TcpBic TcpNewReno; do
  ./ns3 run "scratch/tcp_multiple_flow --expname=${tcp}_udp --tcp_variant=${tcp} --change_delay=false --enable_udp=true --sim_time=60"
done
