//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//


#include "Scheduler.hpp"


#include <algorithm>
#include <vector>


static std::vector<VMId_t> g_vms;
static std::vector<MachineId_t> g_machines;


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
   unsigned total = Machine_GetTotal();
   SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total), 3);
   SimOutput("Scheduler::Init(): Initializing scheduler", 1);
   
   // Create VMs for all machines, matching their CPU types
   for (unsigned i = 0; i < total; i++)
   {
       MachineId_t mid = MachineId_t(i);
       MachineInfo_t mi = Machine_GetInfo(mid);
       
       // Turn on the machine
       Machine_SetState(mid, S0);
       
       // Create a LINUX VM with the machine's CPU type
       VMId_t vm = VM_Create(LINUX, mi.cpu);
       VM_Attach(vm, mid);
       
       vms.push_back(vm);
       machines.push_back(mid);
   }

   active_machines = total;
   
   bool dynamic = false;
   if (dynamic)
       for (unsigned i = 0; i < 4; i++)
           for (unsigned j = 0; j < 8; j++)
               Machine_SetCorePerformance(MachineId_t(0), j, P3);

   g_vms = vms;
   g_machines = machines;


   SimOutput("Scheduler::Init(): VM ids are " + to_string(vms[0]) + " ahd " + to_string(vms[1]), 3);
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
   Priority_t priority = assign_sla_priority(t.required_sla);


   // pick best machine among the active set [0..active_machines-1]
   MachineId_t best = MachineId_t(0);
   bool found = false;
   unsigned best_load = (1u << 30);


   for (unsigned i = 0; i < active_machines; ++i)
   {
       MachineId_t m = machines[i];
       auto mi = Machine_GetInfo(m);


       // must match required CPU
       if (mi.cpu != t.required_cpu)
           continue;


       // host must be up (avoid sleeping or off)
       if (!(mi.s_state == S0 || mi.s_state == S0i1))
           continue;


       // memory headroom for VM overhead + task
       unsigned need = VM_MEMORY_OVERHEAD + t.required_memory;
       if (mi.memory_size < mi.memory_used + need)
           continue;


       // choose least loaded
       if (mi.active_tasks < best_load)
       {
           best_load = mi.active_tasks;
           best = m;
           found = true;
       }
   }


   // if not found with headroom, fall back to least-loaded compatible active machine
   if (!found)
   {
       for (unsigned i = 0; i < active_machines; ++i)
       {
           MachineId_t m = machines[i];
           auto mi = Machine_GetInfo(m);
           if (mi.cpu != t.required_cpu)
               continue;
           if (!(mi.s_state == S0 || mi.s_state == S0i1))
               continue;
           if (mi.active_tasks < best_load)
           {
               best_load = mi.active_tasks;
               best = m;
               found = true;
           }
       }
   }


   if (!found)
       best = machines[0];

   unsigned vm_index = 0;
   for (unsigned i = 0; i < active_machines; ++i)
   {
       if (machines[i] == best)
       {
           vm_index = i;
           break;
       }
   }


   VMId_t vm = vms[vm_index];
   VM_AddTask(vm, task_id, priority);


   // boost host to P0 if strict SLA
   if (t.required_sla == SLA0 || t.required_sla == SLA1)
   {
       set_machine_pstate(best, P0);
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
   
}



void StateChangeComplete(Time_t time, MachineId_t machine_id)
{
   // Called in response to an earlier request to change the state of a machine
}


