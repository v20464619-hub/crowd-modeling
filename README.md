# NS-3 Assignment 7

This folder contains the completed NS-3 scratch programs for assignment 7:

- `tutdgr.cc`: Dynamic Global Routing with interface-event recomputation.
- `tutrip.cc`: RIP on backbone routers and static default routes on hosts.
- `topology.json`: The configurable topology, flows, and link failure.
- `ns3_part7_analysis.ipynb`: Notebook for plots, metrics, and written answers.

## Run on Ubuntu 22.04 with NS-3.42

Place these files in `ns-3.42/scratch`, then run from the `ns-3.42` root:

```bash
mkdir -p output/data/dgr output/data/rip
./ns3 build
./ns3 run 'tutdgr --config=scratch/topology.json --outputDir=output/data/dgr'
./ns3 run 'tutrip --config=scratch/topology.json --outputDir=output/data/rip'
```

The programs also accept `--monitorInterval=<seconds>` for the flow-monitor time series.

Generated files:

- `output/data/dgr/flowmon.xml`
- `output/data/dgr/flow-stats-timeseries.csv`
- `output/data/dgr/ping-rtt.csv`
- `output/data/rip/flowmon.xml`
- `output/data/rip/flow-stats-timeseries.csv`
- `output/data/rip/ping-rtt.csv`
- `output/data/rip/routes-<node>.txt`

For the final archive layout requested in the assignment, include the generated `data/dgr` and `data/rip` folders, or rename/copy `output/data` to `data` before archiving.
