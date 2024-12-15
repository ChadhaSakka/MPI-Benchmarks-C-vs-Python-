# MPI Benchmarking: C vs Python

## Overview
This repository contains the results and analysis for a comparison of MPI performance benchmarks written in C and Python. The focus was on bi-directional bandwidth, measured using:
- `osu_bibw.c` (compiled and executed for C benchmarks).
- `osu_bibw.py` (executed for Python benchmarks).

The benchmarks were executed on a two-node MPI cluster using Docker containers.

## Contents
- `report.pdf`: Detailed report comparing C and Python benchmarks.
- `results_c.txt`: Results of the C benchmark (osu\_bibw).
- `results_python.txt`: Results of the Python benchmark (osu\_bibw.py).
- `plot_results.py`: Python script to visualize the results.

## Instructions to Reproduce (Without Proprietary Code)
1. Obtain the necessary benchmark code (e.g., `osu_bibw.c` and `osu_bibw.py`) from your instructor or course materials.
2. Set up a Docker-based MPI environment.
3. Follow the steps outlined in the report or below:
   - Compile the C code: `mpicc -o osu_bibw osu_bibw.c`.
   - Run the benchmarks:
     - **C**: `mpirun --mca btl_tcp_if_include eth0 -np 2 -host <node1>,<node2> ./osu_bibw > results_c.txt`
     - **Python**: `mpirun --mca btl_tcp_if_include eth0 -np 2 -host <node1>,<node2> python3 ./osu_bibw.py > results_python.txt`.

4. Use `plot_results.py` to generate visual comparisons of the bandwidth results.
