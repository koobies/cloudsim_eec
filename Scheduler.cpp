//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <vector>
#include <algorithm>

using std::to_string;
using std::vector;

static bool migrating = false;
static int migrating_vm_index = -1; // the entry in vms that is currently migrating

// Helper to map SLA to scheduling priority
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

// Set P-state based on machine load
static void set_pstate_by_load(MachineId_t m)
{
    MachineInfo_t mi = Machine_GetInfo(m);
    
    // Calculate per-core load
    double tasks_per_core = mi.num_cpus ? 
        double(mi.active_tasks) / double(mi.num_cpus) : 0.0;
    
    CPUPerformance_t new_pstate;
    
    // DVFS policy: Scale frequency based on load
    if (tasks_per_core > 1.5)
    {
        // High load: Run at full speed (P0)
        new_pstate = P0;
    }
    else if (tasks_per_core > 0.8)
    {
        // Medium-high load: Run at 3/4 speed (P1)
        new_pstate = P1;
    }
    else if (tasks_per_core > 0.3)
    {
        // Medium load: Run at 1/2 speed (P2)
        new_pstate = P2;
    }
    else
    {
        // Low load: Run at 1/4 speed (P3)
        new_pstate = P3;
    }
    
    // Set all cores to the same P-state
    for (unsigned c = 0; c < mi.num_cpus; ++c)
    {
        Machine_SetCorePerformance(m, c, new_pstate);
    }
}

void Scheduler::Init()
{
    SimOutput("Scheduler::Init(): Total number of machines is " +
                  to_string(Machine_GetTotal()),
              3);
    SimOutput("Scheduler::Init(): Initializing GREEDY+MIGRATION scheduler", 1);

    vms.clear();
    machines.clear();

    unsigned total = Machine_GetTotal();

    // Create VMs for all CPU types, power them on
    // For every machine, power on and create one LINUX VM matching its CPU type
    for (unsigned i = 0; i < total; ++i)
    {
        MachineId_t mid = MachineId_t(i);
        MachineInfo_t mi = Machine_GetInfo(mid);

        // Turn on all machines
        Machine_SetState(mid, S0); 

        // Create a LINUX VM with the machine's CPU type
        VMId_t vm = VM_Create(LINUX, mi.cpu);
        VM_Attach(vm, mid);

        vms.push_back(vm);
        machines.push_back(mid);
    }

    if (!vms.empty())
    {
        SimOutput("Scheduler::Init(): Created " + to_string(vms.size()) +
                      " VMs for greedy scheduler (all CPU types)",
                  2);
    }
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id)
{
    (void)time;

    // Update machines mapping so vms stays aligned with machines
    for (unsigned i = 0; i < vms.size(); ++i)
    {
        if (vms[i] == vm_id)
        {
            VMInfo_t vi = VM_GetInfo(vm_id);
            machines[i] = vi.machine_id; 
            break;
        }
    }
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id)
{
    (void)now;

    TaskInfo_t t = GetTaskInfo(task_id);
    Priority_t priority = prio_for_sla(t.required_sla);

    CPUType_t need_cpu = t.required_cpu;
    unsigned need_mem = t.required_memory;

    int best_index = -1;
    double best_score = -1.0;

    unsigned active_machines = machines.size();
    if (active_machines == 0)
    {
        SimOutput("Scheduler::NewTask(): No machines available!", 0);
        return;
    }

    // greedy best-fit packing by memory utilization
    for (unsigned i = 0; i < active_machines; ++i)
    {
        // Don't place tasks on a VM that is currently migrating
        if (migrating && (int)i == migrating_vm_index)
            continue;

        MachineId_t m = machines[i];
        MachineInfo_t mi = Machine_GetInfo(m);

        // CPU type has to match
        if (mi.cpu != need_cpu)
            continue;

        // Only consider machines that are up
        if (!(mi.s_state == S0 || mi.s_state == S0i1))
            continue;

        // Check memory capacity
        if (mi.memory_size < mi.memory_used + need_mem)
            continue;

        // Approximate load and memory utilization if task is placed here
        double tasks_per_core =
            mi.num_cpus ? double(mi.active_tasks + 1) / double(mi.num_cpus) : 0.0;
        double mem_fill =
            mi.memory_size ? double(mi.memory_used + need_mem) / double(mi.memory_size)
                           : 0.0;

        // Apply some hard limits to avoid overload
        if (mem_fill > 0.90) // don't exceed 90% memory
            continue;
        if (tasks_per_core > 2.) // at most 2 tasks per core
            continue;

        // Best-fit by memory util
        double score = mem_fill;
        if (score > best_score)
        {
            best_score = score;
            best_index = int(i);
        }
    }

    // if a good slot was not found, fall back to least loaded
    if (best_index < 0)
    {
        unsigned least_load = (1u << 30);

        for (unsigned i = 0; i < active_machines; ++i)
        {
            if (migrating && (int)i == migrating_vm_index)
                continue;

            MachineId_t m = machines[i];
            MachineInfo_t mi = Machine_GetInfo(m);

            if (mi.cpu != need_cpu)
                continue;
            if (!(mi.s_state == S0 || mi.s_state == S0i1))
                continue;
            if (mi.memory_size < mi.memory_used + need_mem)
                continue;

            if (mi.active_tasks < least_load)
            {
                least_load = mi.active_tasks;
                best_index = int(i);
            }
        }
    }

    // Final fallback is to just drop it on index 0 if it's not migrating
    if (best_index < 0)
    {
        if (migrating && migrating_vm_index == 0 && active_machines > 1)
            best_index = 1;
        else
            best_index = 0;
    }

    VMId_t vm = vms[best_index];
    VM_AddTask(vm, task_id, priority);

    SimOutput("Scheduler::NewTask(): Greedy placed task " + to_string(task_id) +
                  " on machine " + to_string(machines[best_index]) +
                  " with priority " + to_string(priority),
              3);
    
    // Apply DVFS based on the updated machine load
    set_pstate_by_load(machines[best_index]);
}

void Scheduler::PeriodicCheck(Time_t now)
{
    (void)now;
    unsigned active_machines = machines.size();
    if (active_machines == 0)
        return;

    // Apply DVFS to all machines based on their current load
    for (unsigned i = 0; i < active_machines; ++i)
    {
        MachineId_t m = machines[i];
        MachineInfo_t mi = Machine_GetInfo(m);
        
        if (!(mi.s_state == S0 || mi.s_state == S0i1))
            continue;
        
        set_pstate_by_load(m);
    }

    // Only one migration at a time
    if (migrating)
        return;

    int src = -1;
    int dst = -1;
    double max_load = -1.0;
    double min_load = 1e30;

    // Compute loads and find most overloaded and most underloaded machines
    for (unsigned i = 0; i < active_machines; ++i)
    {
        MachineId_t m = machines[i];
        MachineInfo_t mi = Machine_GetInfo(m);

        if (!(mi.s_state == S0 || mi.s_state == S0i1))
            continue;

        double tasks_per_core =
            mi.num_cpus ? double(mi.active_tasks) / double(mi.num_cpus) : 0.0;

        if (tasks_per_core > max_load)
        {
            max_load = tasks_per_core;
            src = int(i);
        }

        if (tasks_per_core < min_load)
        {
            min_load = tasks_per_core;
            dst = int(i);
        }
    }

    // Thresholds for overloaded and underloaded
    const double OVERLOAD = 1.5; 
    const double UNDERLOAD = 0.3; 

    if (src >= 0 && dst >= 0 && src != dst &&
        max_load > OVERLOAD && min_load < UNDERLOAD)
    {
        VMId_t vm_src = vms[src];
        MachineId_t m_dst = machines[dst];

        VMInfo_t vinfo = VM_GetInfo(vm_src);
        MachineInfo_t mi_dst = Machine_GetInfo(m_dst);

        // Make sure CPU types match
        if (vinfo.cpu == mi_dst.cpu)
        {
            SimOutput("PeriodicCheck(): Migrating VM " + to_string(vm_src) +
                          " from machine " + to_string(machines[src]) +
                          " to machine " + to_string(m_dst),
                      2);

            VM_Migrate(vm_src, m_dst);
            migrating = true;
            migrating_vm_index = src;
        }
    }
}

void Scheduler::Shutdown(Time_t time)
{
    for (auto &vm : vms)
    {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id)
{
    (void)now;
    (void)task_id;
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) +
                  " is complete",
              4);
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
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) +
                  " at time " + to_string(time),
              4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id)
{
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) +
                  " completed at time " + to_string(time),
              4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id)
{

    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) +
                  " detected at time " + to_string(time),
              0);
}

void MigrationDone(Time_t time, VMId_t vm_id)
{
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) +
                  " completed at time " + to_string(time),
              4);

    // Let the Scheduler object update its private state
    Scheduler.MigrationComplete(time, vm_id);

    // Clear migration flags
    migrating = false;
    migrating_vm_index = -1;
}

void SchedulerCheck(Time_t time)
{
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time),
              4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time)
{
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time) / 1000000 << " seconds"
         << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " +
                  to_string(time),
              4);

    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id)
{
    (void)time;
    SetTaskPriority(task_id, HIGH_PRIORITY);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id)
{
    (void)time;
    (void)machine_id;
}
