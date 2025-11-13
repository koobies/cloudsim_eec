
machine class:
{
Number of machines: 4
CPU type: X86
Number of cores: 16
Memory: 65536
S-States: [220, 180, 150, 120, 80, 20, 0]
P-States: [18, 12, 8, 4]
C-States: [18, 4, 2, 0]
MIPS: [1500, 1200, 900, 500]
GPUs: yes
}

machine class:
{
Number of machines: 12
CPU type: X86
Number of cores: 4
Memory: 8192
S-States: [60, 45, 35, 25, 15, 5, 0]
P-States: [8, 5, 3, 2]
C-States: [8, 2, 1, 0]
MIPS: [800, 600, 400, 200]
GPUs: no
}

task class:
{
Start time: 60000
End time: 800000
Inter arrival: 8000
Expected runtime: 150000
Memory: 4
VM type: LINUX
GPU enabled: no
SLA type: SLA0
CPU type: X86
Task type: WEB
Seed: 111111
}


task class:
{
Start time: 60000
End time: 800000
Inter arrival: 12000
Expected runtime: 500000
Memory: 32
VM type: LINUX
GPU enabled: yes
SLA type: SLA1
CPU type: X86
Task type: AI
Seed: 222222
}


task class:
{
Start time: 60000
End time: 800000
Inter arrival: 7000
Expected runtime: 300000
Memory: 2
VM type: LINUX
GPU enabled: no
SLA type: SLA2
CPU type: X86
Task type: STREAM
Seed: 333333
}


task class:
{
Start time: 60000
End time: 800000
Inter arrival: 20000
Expected runtime: 1200000
Memory: 16
VM type: LINUX
GPU enabled: no
SLA type: SLA3
CPU type: X86
Task type: HPC
Seed: 444444
}
