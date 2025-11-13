
machine class:
{
Number of machines: 16
CPU type: X86
Number of cores: 8
Memory: 16384
S-States: [120, 100, 80, 60, 40, 15, 0]
P-States: [12, 8, 6, 4]
C-States: [12, 3, 1, 0]
MIPS: [1000, 800, 600, 400]
GPUs: yes
}


task class:
{
Start time: 0
End time: 1200000
Inter arrival: 40000
Expected runtime: 800000
Memory: 8
VM type: LINUX
GPU enabled: no
SLA type: SLA3
CPU type: X86
Task type: STREAM
Seed: 555555
}




task class:
{
Start time: 60000
End time: 1200000
Inter arrival: 15000
Expected runtime: 200000
Memory: 4
VM type: LINUX
GPU enabled: no
SLA type: SLA2
CPU type: X86
Task type: WEB
Seed: 666666
}




task class:
{
Start time: 300000
End time: 450000
Inter arrival: 3000
Expected runtime: 180000
Memory: 6
VM type: LINUX
GPU enabled: no
SLA type: SLA0
CPU type: X86
Task type: WEB
Seed: 777777
}




task class:
{
Start time: 320000
End time: 460000
Inter arrival: 5000
Expected runtime: 600000
Memory: 24
VM type: LINUX
GPU enabled: yes
SLA type: SLA1
CPU type: X86
Task type: AI
Seed: 888888
}



