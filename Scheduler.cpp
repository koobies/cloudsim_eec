//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

#include <vector>

using std::to_string;
using std::vector;

static bool migrating = false;
static unsigned active_machines = 0;

// round robin pointer
static unsigned rr_index = 0;

// SLA to priority mapping
static Priority_t assign_sla_priority(SLAType_t s)
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

// Return true if VM has any SLA0 or SLA1 tasks
static bool vm_has_strict_sla(VMId_t vm)
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
    MachineInfo_t mi = Machine_GetInfo(m);
    for (unsigned c = 0; c < mi.num_cpus; ++c)
    {
        Machine_SetCorePerformance(m, c, p);
    }
}

void Scheduler::Init()
{
    SimOutput("Scheduler::Init(): Total number of machines is " +
                  to_string(Machine_GetTotal()),
              3);
    SimOutput("Scheduler::Init(): Initializing round robin scheduler", 1);

    vms.clear();
    machines.clear();
    rr_index = 0;
    active_machines = 0;

    unsigned total = Machine_GetTotal();

    // Create one VM per machine, with a CPU type that matches the machine
    // Use LINUX on X86/ARM, AIX on POWER
    for (unsigned i = 0; i < total; ++i)
    {
        MachineId_t mid = MachineId_t(i);
        MachineInfo_t mi = Machine_GetInfo(mid);

        // Power on every machine to be used
        Machine_SetState(mid, S0);

        VMType_t vm_type;
        CPUType_t cpu_type = mi.cpu;

        if (mi.cpu == POWER)
        {
            vm_type = AIX;
        }
        else
        {
            vm_type = LINUX;
        }

        VMId_t vm = VM_Create(vm_type, cpu_type);
        VM_Attach(vm, mid);

        vms.push_back(vm);
        machines.push_back(mid);
        active_machines++;
    }

    if (active_machines == 0)
    {
        SimOutput("Scheduler::Init(): No machines available", 0);
        return;
    }

    SimOutput("Scheduler::Init(): Created " + to_string(active_machines) +
                  " VMs for round robin scheduler",
              2);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id)
{
    (void)time;
    (void)vm_id;
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id)
{
    (void)now;

    if (active_machines == 0)
    {
        SimOutput("Scheduler::NewTask(): No machines available", 0);
        return;
    }

    TaskInfo_t t = GetTaskInfo(task_id);
    Priority_t priority = assign_sla_priority(t.required_sla);

    bool need_gpu = t.gpu_capable;

    int chosen_index = -1;
    unsigned need_mem = VM_MEMORY_OVERHEAD + t.required_memory;

    // scan the RR ring once and pick the first fully compatible VM:
    // CPU match, VM type match, GPU requirement satisfied, enough memory, machine on.
    for (unsigned offset = 0; offset < active_machines; ++offset)
    {
        unsigned i = (rr_index + offset) % active_machines;

        MachineId_t mid = machines[i];
        MachineInfo_t mi = Machine_GetInfo(mid);
        VMInfo_t vminfo = VM_GetInfo(vms[i]);

        // CPU type must match
        if (mi.cpu != t.required_cpu)
            continue;

        // VM type must match required VM
        if (vminfo.vm_type != t.required_vm)
            continue;

        // machine must be on
        if (!(mi.s_state == S0 || mi.s_state == S0i1))
            continue;

        // GPU requirement: if task needs GPU, machine must have one
        if (need_gpu && !mi.gpus)
            continue;

        // memory capacity check
        if (mi.memory_size < mi.memory_used + need_mem)
            continue;

        chosen_index = (int)i;
        break;
    }

    // relax memory requirement but still enforce CPU, VM, GPU, and state
    if (chosen_index < 0)
    {
        for (unsigned offset = 0; offset < active_machines; ++offset)
        {
            unsigned i = (rr_index + offset) % active_machines;

            MachineId_t mid = machines[i];
            MachineInfo_t mi = Machine_GetInfo(mid);
            VMInfo_t vminfo = VM_GetInfo(vms[i]);

            if (mi.cpu != t.required_cpu)
                continue;

            if (vminfo.vm_type != t.required_vm)
                continue;

            if (!(mi.s_state == S0 || mi.s_state == S0i1))
                continue;

            if (need_gpu && !mi.gpus)
                continue;

            chosen_index = (int)i;
            break;
        }
    }

    // if there is still no compatible VM, bail
    if (chosen_index < 0)
    {
        SimOutput("Scheduler::NewTask(): No compatible VM found for task " +
                      to_string(task_id),
                  0);
        return;
    }

    VMId_t vm = vms[chosen_index];
    VM_AddTask(vm, task_id, priority);

    // advance rr_index for next task
    rr_index = (chosen_index + 1) % active_machines;

    SimOutput("Scheduler::NewTask(): RR placed task " + to_string(task_id) +
                  " on machine " + to_string(machines[chosen_index]) +
                  " with priority " + to_string(priority),
              3);
}



void Scheduler::PeriodicCheck(Time_t now)
{
    (void)now;

    if (active_machines == 0)
        return;

    // Simple DVFS
    //  If a VM has any SLA0/1 tasks, keep host at P0 for performance
    //  else scale by load - high load P0, medium P1, low P2
    for (unsigned i = 0; i < active_machines; ++i)
    {
        VMId_t vm = vms[i];
        VMInfo_t vinfo = VM_GetInfo(vm);
        MachineId_t mid = vinfo.machine_id;
        MachineInfo_t mi = Machine_GetInfo(mid);

        if (!(mi.s_state == S0 || mi.s_state == S0i1))
            continue;

        bool strict = vm_has_strict_sla(vm);
        if (strict)
        {
            set_machine_pstate(mid, P0);
            continue;
        }

        double tasks_per_core =
            mi.num_cpus ? double(mi.active_tasks) / double(mi.num_cpus) : 0.0;

        if (tasks_per_core > 0.8)
            set_machine_pstate(mid, P0);
        else if (tasks_per_core > 0.3)
            set_machine_pstate(mid, P1);
        else
            set_machine_pstate(mid, P2);
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
                  " was detected at time " + to_string(time),
              0);
}

void MigrationDone(Time_t time, VMId_t vm_id)
{
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) +
                  " was completed at time " + to_string(time),
              4);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
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
