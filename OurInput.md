# Heterogeneous, stressful test input for comparing Round Robin vs Greedy
# - First 16 machines are X86 and used by your Scheduler::Init()
# - 8 "slow" X86 nodes (few cores, low MIPS)
# - 8 "fast" X86 nodes (many cores, high MIPS)
# - Heavy SLA0 + SLA1 load and frequent SLA2 tasks


# 8 slow X86 machines (IDs 0..7)
machine class:
{
   Number of machines: 8
   CPU type: X86
   Number of cores: 2
   Memory: 8192
   S-States: [120, 100, 100, 80, 40, 10, 0]
   P-States: [12, 8, 6, 4]
   C-States: [12, 3, 1, 0]
   MIPS: [400, 300, 200, 100]    
   GPUs: no
}


# 8 fast X86 machines (IDs 8..15)
machine class:
{
   Number of machines: 8
   CPU type: X86
   Number of cores: 16
   Memory: 32768
   S-States: [120, 100, 100, 80, 40, 10, 0]
   P-States: [12, 8, 6, 4]
   C-States: [12, 3, 1, 0]
   MIPS: [2000, 1500, 1200, 800] 
   GPUs: no
}


# (Optional) extra machines beyond ID 15 – your current Init() ignores these,
# but they show that the cluster COULD be larger without affecting 0..15.


machine class:
{
   Number of machines: 8
   CPU type: X86
   Number of cores: 8
   Memory: 16384
   S-States: [120, 100, 100, 80, 40, 10, 0]
   P-States: [12, 8, 6, 4]
   C-States: [12, 3, 1, 0]
   MIPS: [1000, 800, 600, 400]
   GPUs: no
}


# --------------- Task classes (stressful load) ---------------


# SLA0: strict, heavy tasks – long runtime, fairly fast arrivals
task class:
{
   Start time: 60000
   End time : 260000
   Inter arrival: 2000         
   Expected runtime: 30000000 
   Memory: 512
   VM type: LINUX
   GPU enabled: no
   SLA type: SLA0
   CPU type: X86
   Task type: WEB
   Seed: 530001
}


# SLA1: also heavy, slightly less strict
task class:
{
   Start time: 60000
   End time : 260000
   Inter arrival: 3000        
   Expected runtime: 20000000
   Memory: 256
   VM type: LINUX
   GPU enabled: no
   SLA type: SLA1
   CPU type: X86
   Task type: WEB
   Seed: 530002
}


# SLA2: many lighter web-like tasks – add background pressure
task class:
{
   Start time: 60000
   End time : 260000
   Inter arrival: 1000        
   Expected runtime: 3000000   
   Memory: 64
   VM type: LINUX
   GPU enabled: no
   SLA type: SLA2
   CPU type: X86
   Task type: WEB
   Seed: 530003
}



