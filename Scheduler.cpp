//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <algorithm>
#include <vector>

// Use S0 platform power + per-core P0 power * num_cpus as a baseline score.
static double machine_energy_score(const MachineInfo_t &mi)
{
    unsigned s0_power = mi.s_states.size() ? mi.s_states[0] : 120;
    unsigned p0_power = mi.p_states.size() ? mi.p_states[0] : 12;
    unsigned cores = mi.num_cpus ? mi.num_cpus : 1;
    return double(s0_power) + double(p0_power) * double(cores);
}

static std::vector<VMId_t> g_vms;
static std::vector<MachineId_t> g_machines;

static Priority_t prio_for_sla(SLAType_t s)
{
    switch (s)
    {
    case SLA0:
        return HIGH_PRIORITY;
    case SLA1:
        return MID_PRIORITY;
    default:
        return LOW_PRIORITY;
    }
}

// Return true if machine has any SLA0 or SLA1 tasks
static bool machine_has_strict_sla(MachineId_t m, VMId_t vm)
{
    VMInfo_t vinfo = VM_GetInfo(vm);
    for (auto tid : vinfo.active_tasks)
    {
        SLAType_t s = RequiredSLA(tid);
        if (s == SLA0 || s == SLA1)
            return true;
    }
    return false;
}

// Set P-state for all cores on a machine
static void set_machine_pstate(MachineId_t m, CPUPerformance_t p)
{
    auto mi = Machine_GetInfo(m);
    for (unsigned c = 0; c < mi.num_cpus; ++c)
    {
        Machine_SetCorePerformance(m, c, p);
    }
}

static bool migrating = false;
static unsigned active_machines = 16;

void Scheduler::Init()
{
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    //
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler (pMapper)", 1);

    unsigned total = Machine_GetTotal();

    // First, create a VM for each X86 machine and collect their info
    struct Entry
    {
        MachineId_t m;
        VMId_t vm;
        double score;
    };
    std::vector<Entry> entries;

    for (unsigned i = 0; i < total; ++i)
    {
        MachineId_t mid = MachineId_t(i);
        MachineInfo_t mi = Machine_GetInfo(mid);

        if (mi.cpu == X86)
        {
            // turn X86 machines on (S0) for now
            Machine_SetState(mid, S0);

            // create a LINUX/X86 VM and attach
            VMId_t vm = VM_Create(LINUX, X86);
            VM_Attach(vm, mid);

            double score = machine_energy_score(mi);
            entries.push_back({mid, vm, score});
        }
        else
        {
            // turn off non-X86 machines (ARM cluster)
            Machine_SetState(mid, S5);
        }
    }

    // pMapper: sort machines by energy consumption (lowest to highest)
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b)
              { return a.score < b.score; });

    // Clear the existing vectors and rebuild them in pMapper order
    vms.clear();
    machines.clear();

    for (auto &e : entries)
    {
        machines.push_back(e.m);
        vms.push_back(e.vm);
    }

    active_machines = machines.size(); // number of X86 machines
    g_vms = vms;
    g_machines = machines;

    if (!vms.empty())
    {
        SimOutput("Scheduler::Init(): pMapper order, first VM id is " +
                      to_string(vms[0]) + " on machine " + to_string(machines[0]),
                  3);
    }
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id)
{
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id)
{
    // Get the task parameters
    //  IsGPUCapable(task_id);
    //  GetMemory(task_id);
    //  RequiredVMType(task_id);
    //  RequiredSLA(task_id);
    //  RequiredCPUType(task_id);
    // Decide to attach the task to an existing VM,
    //      vm.AddTask(taskid, Priority_T priority); or
    // Create a new VM, attach the VM to a machine
    //      VM vm(type of the VM)
    //      vm.Attach(machine_id);
    //      vm.AddTask(taskid, Priority_t priority) or
    // Turn on a machine, create a new VM, attach it to the VM, then add the task
    //
    // Turn on a machine, migrate an existing VM from a loaded machine....
    //
    // Other possibilities as desired
    // PerformanceFirst placement:
    // 1) prioritize SLA0, then SLA1 via priority
    // 2) choose least-loaded compatible active machine with memory headroom
    // 3) set host to P0 if SLA0 or SLA1

    TaskInfo_t t = GetTaskInfo(task_id);
    Priority_t priority = prio_for_sla(t.required_sla);

    // pMapper placement:
    // - machines[] is already sorted by energy score (Init did that)
    // - we try them in that order and pick the first with enough capacity

    MachineId_t chosen = machines[0];
    bool found = false;
    unsigned best_index = 0;

    for (unsigned i = 0; i < active_machines; ++i)
    {
        MachineId_t m = machines[i];
        MachineInfo_t mi = Machine_GetInfo(m);

        // Must match required CPU
        if (mi.cpu != t.required_cpu)
            continue;

        // Machine must be up
        if (!(mi.s_state == S0 || mi.s_state == S0i1))
            continue;

        // Memory check (VM overhead + task memory)
        unsigned need = VM_MEMORY_OVERHEAD + t.required_memory;
        if (mi.memory_size < mi.memory_used + need)
            continue;

        // pMapper capacity approximation: u + v < 1 ~ active_tasks < num_cpus
        if (mi.active_tasks < mi.num_cpus) {
            chosen = m;
            best_index = i;
            found = true;
            break;
        }
    }

    // If none satisfied the "capacity" condition, fall back to least loaded machine
    if (!found)
    {
        unsigned least_load = (1u << 30);
        for (unsigned i = 0; i < active_machines; ++i)
        {
            MachineId_t m = machines[i];
            MachineInfo_t mi = Machine_GetInfo(m);
            if (mi.cpu != t.required_cpu)
                continue;
            if (!(mi.s_state == S0 || mi.s_state == S0i1))
                continue;

            if (mi.active_tasks < least_load) {
                least_load = mi.active_tasks;
                chosen = m;
                best_index = i;
            }
        }
    }

    // Use the VM attached to the chosen machine (same index)
    VMId_t vm = vms[best_index];
    VM_AddTask(vm, task_id, priority);

    // Optionally: if strict SLA, boost this machine to P0 for performance
    if (t.required_sla == SLA0 || t.required_sla == SLA1)
    {
        set_machine_pstate(chosen, P0);
    }
}


void Scheduler::PeriodicCheck(Time_t now)
{
    // Performance-first DVFS policy:
    // - If machine has any SLA0/1 tasks, keep P0
    // - Else if busy, P1; else P2

    for (unsigned i = 0; i < active_machines; ++i)
    {
        MachineId_t m = machines[i];
        auto mi = Machine_GetInfo(m);
        if (!(mi.s_state == S0 || mi.s_state == S0i1))
            continue;

        // Check strict SLA presence on our single VM per machine
        bool strict = machine_has_strict_sla(m, vms[i]);
        if (strict)
        {
            set_machine_pstate(m, P0);
            continue;
        }

        // no strict SLA tasks; scale by load
        double tasks_per_core = mi.num_cpus ? double(mi.active_tasks) / double(mi.num_cpus) : 0.0;
        if (tasks_per_core > 0.8)
            set_machine_pstate(m, P0);
        else if (tasks_per_core > 0.3)
            set_machine_pstate(m, P1);
        else
            set_machine_pstate(m, P2);
    }
}

void Scheduler::Shutdown(Time_t time)
{
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for (auto &vm : vms)
    {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id)
{
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler()
{
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id)
{
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id)
{
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id)
{
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id)
{
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
}

void SchedulerCheck(Time_t time)
{
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time)
{
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl; // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time) / 1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);

    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id)
{
    // Performance-first SLA handler:
    // Find the machine running this task and boost its P-state to P0 (max speed).

    if (g_vms.empty() || g_machines.empty())
        return;

    // Find which VM currently hosts the task
    unsigned host_idx = (unsigned)~0u;
    for (unsigned i = 0; i < g_vms.size(); ++i)
    {
        VMInfo_t vi = VM_GetInfo(g_vms[i]);
        for (auto tid : vi.active_tasks)
        {
            if (tid == task_id)
            {
                host_idx = i;
                break;
            }
        }
        if (host_idx != (unsigned)~0u)
            break;
    }
    if (host_idx == (unsigned)~0u)
        return;

    // Get the correct host machine id
    MachineId_t host = g_machines[host_idx];

    // Boost all cores on this machine to highest performance
    auto mi_host = Machine_GetInfo(host);
    for (unsigned c = 0; c < mi_host.num_cpus; ++c)
    {
        Machine_SetCorePerformance(host, c, P0);
    }

    SetTaskPriority(task_id, HIGH_PRIORITY);

    SimOutput("SLAWarning(): boosted machine " + std::to_string(host) + " to P0 for task " + std::to_string(task_id), 2);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id)
{
    // Called in response to an earlier request to change the state of a machine
}
