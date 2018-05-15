## ProgrAMR: An AMR task graph analysis tool

### Prerequisites: 
- C++11 compliant compiler
- If you also want to couple generated task graph from ProgrAMR with the [Mota Task Mapping Library](https://github.com/LBL-CoDEx/mota) for task mapping impact analysis:
  - Clone [Mota source tree](https://github.com/LBL-CoDEx/mota.git) into parent directory (at same level as ProgrAMR source tree)

### Sample Analysis:
- To generate a task graph for a multigrid execution:
  - Run: `events=1 ./run app/mg_simple.cxx`
- To run task placement algorithms with Mota Library using the generated task graph from ProgrAMR:
  - Change `flag_force_traffic = true` in `mota/src/flags.hxx` for more network model detail
  - Run: `mapper=1 PROGRAMR_KNOB_MOTA=1 ./run app/mg_simple.cxx`

## Copyright

"ProgrAMR: An AMR task graph analysis tool" Copyright (c) 2018, The Regents of the University of California, through Lawrence Berkeley National Laboratory (subject to receipt of any required approvals from the U.S. Dept. of Energy).  All rights reserved.

If you have questions about your rights to use or distribute this software, please contact Berkeley Lab's Innovation & Partnerships Office at IPO@lbl.gov.

NOTICE.  This Software was developed under funding from the U.S. Department of Energy and the U.S. Government consequently retains certain rights. As such, the U.S. Government has been granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable, worldwide license in the Software to reproduce, distribute copies to the public, prepare derivative works, and perform publicly and display publicly, and to permit other to do so.
