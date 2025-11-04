# Small X86 cluster (this is what your scheduler actually uses)
machine class:
{
        Number of machines: 16
        CPU type: X86
        Number of cores: 4
        Memory: 8192
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [400, 300, 200, 100]
        GPUs: no
}

# ARM cluster (your current scheduler basically ignores these)
machine class:
{
        Number of machines: 24
        CPU type: ARM
        Number of cores: 8
        Memory: 8192
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [400, 300, 200, 100]
        GPUs: no
}

# ---- Task classes ----

# SLA0: very heavy, strict SLA, long runtime, fairly fast arrivals
task class:
{
        Start time: 60000
        End time : 260000
        Inter arrival: 2000
        Expected runtime: 20000000
        Memory: 512
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 520100
}

# SLA1: still heavy, slightly less strict
task class:
{
        Start time: 60000
        End time : 260000
        Inter arrival: 3000
        Expected runtime: 10000000
        Memory: 256
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: X86
        Task type: WEB
        Seed: 520110
}

# SLA2: many lighter web tasks that add extra load
task class:
{
        Start time: 60000
        End time : 260000
        Inter arrival: 1000
        Expected runtime: 2000000
        Memory: 64
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 520120
}
