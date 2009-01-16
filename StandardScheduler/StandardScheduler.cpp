#include "StandardScheduler.h"#include <mach/mach_types.h>#include <mach/mach_init.h>#include <mach/thread_policy.h>#include <mach/task_policy.h>#include <mach/thread_act.h>#include <sys/sysctl.h>int set_realtime(int period, int computation, int constraint) {    struct thread_time_constraint_policy ttcpolicy;    int ret;    ttcpolicy.period=period; // HZ/160    ttcpolicy.computation=computation; // HZ/3300;    ttcpolicy.constraint=constraint; // HZ/2200;    ttcpolicy.preemptible=1;    if ((ret=thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (int *)&ttcpolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT)) != KERN_SUCCESS)     {            mprintf("Set realtime failed (%d)", ret);            return 0;    }    return 1;}void *scheduledExecutionThread(void *arglist){		ThreadArgumentList *args = (ThreadArgumentList *)arglist;	ScheduleTask *task = args->task;	        	int bus_speed, mib [2] = { CTL_HW, HW_BUS_FREQ };	size_t len;   	len = sizeof (bus_speed);	sysctl (mib, 2, &bus_speed, &len, NULL, 0);        	#ifdef REALTIME_SCHEDULER		set_realtime(bus_speed/1000, bus_speed/4000, bus_speed/2000);	#endif                //	long allowed_slop_time = 5; // kludge: do something more intelligent here        	bool primed = true;        	while(task->isActive() || primed ){		primed = false;				if(task->hasPendingSignal()){			//task->setPendingSignal(false);			task->setExecuting(true);			(args->function)(args->argument); // fire at will			task->decrementPendingSignal();			task->setExecuting(false);			// Should raise an alarm here as well, since this 			// indicates that the scheduler is failing		} else {			mprintf("waiting for mutex %d", task);			pthread_mutex_lock(task->getMutex());			pthread_cond_wait(task->getCondition(), task->getMutex());			mprintf("got it %d", task);			task->setExecuting(true);			(args->function)(args->argument);			task->decrementPendingSignal();			task->setExecuting(false);			pthread_mutex_unlock(task->getMutex());		}			}			pthread_exit(0);}//class ScheduleTask::ScheduleTask::ScheduleTask() : ScheduleTask(), LinkedListNode(){    ScheduleTask(0, 0, 1);}ScheduleTask::ScheduleTask(long _initial_delay) : ScheduleTask(), LinkedListNode(){    ScheduleTask(_initial_delay, 0, 1);}ScheduleTask::ScheduleTask(long _repeat_interval, int _ntimes) : ScheduleTask(), LinkedListNode(){    ScheduleTask(0, _repeat_interval, ntimes);}ScheduleTask::ScheduleTask(long _initial_delay, long _repeat_interval, int _ntimes) : ScheduleTask(), LinkedListNode(){        initial_delay = _initial_delay;        repeat_interval = _repeat_interval;                starttime = getCurrentTime();        ntimes = _ntimes;                ndone = 0;        // Add error checking        pthread_cond_init(&condition, NULL);                // Add error checking        pthread_mutex_init(&mutex, NULL);                                active = false;						//pending_signal = false; 		n_pending = 0;		executing = false;		        //alive = true;}void ScheduleTask::tick(long currenttime){	//mprintf("Tick!");	    if(active && currenttime >= starttime + initial_delay + ndone*repeat_interval){        signal();    } }    void ScheduleTask::signal(){        //pthread_cond_signal(&condition);		incrementPendingSignal();		        pthread_cond_broadcast(&condition);        ndone++;                if(ntimes != M_REPEAT_INDEFINITELY && ntimes > 0 && ndone >= ntimes){            //active = false;            kill();            //mprintf("Killing task... (%d)", ntimes);        }}void ScheduleTask::kill(){    active = false;}void ScheduleTask::activate(){        active = true;        if(ndone == 0 && initial_delay == 0){  // signal immediately if no delay            signal();        }}bool ScheduleTask::isActive(){        return active;}bool *ScheduleTask::getAlivePointer(){    return &alive;}pthread_cond_t *ScheduleTask::getCondition(){        return &condition;}pthread_mutex_t *ScheduleTask::getMutex(){        return &mutex;}pthread_t *ScheduleTask::getThread(){        return &thread;}void ScheduleTask::setPriority(int _priority){    struct sched_param sp;    memset(&sp, 0, sizeof(struct sched_param));    sp.sched_priority=_priority;    pthread_setschedparam(thread, SCHED_FIFO, &sp);}void ScheduleTask::setPriority(){    setPriority(TASK_PRIORITY);}// ScheduleTask methodsvoid ScheduleTask::cancel(){    kill();	remove();}void ScheduleTask::pause(){    active = false;}void ScheduleTask::resume(){    active = true;}// class StandardScheduler::StandardScheduler::StandardScheduler():SchedulerFactory(){        schedulelist = new LinkedList();		//schedulelist = new ExpandableList<ScheduleTask>(BASE_NUM_TASKS);                // initialize the direct tick condition var. Important stuff can block and        // wake up on this signal        pthread_cond_init(&direct_tick_condition, NULL);         }// factory methodScheduler *StandardScheduler::createScheduler(){    return (Scheduler *)(new StandardScheduler());}ScheduleTask *StandardScheduler::scheduleMS(long initial_delay, long repeat_interval, int ntimes, void *(*funptr)(void *), void *arg){	return schedule(initial_delay, repeat_interval, ntimes, funptr, arg);}ScheduleTask *StandardScheduler::scheduleUS(long initial_delay, long repeat_interval, int ntimes, void *(*funptr)(void *), void *arg){	return schedule((long)((double)initial_delay/(double)1000), (long)((double)repeat_interval/(double)1000), ntimes, funptr, arg);}ScheduleTask *StandardScheduler::schedule(long initial_delay, long repeat_interval, int ntimes, void *(*funptr)(void *), void *arg){        ThreadArgumentList *args = (ThreadArgumentList *)calloc(1, sizeof(ThreadArgumentList));                // add the schedule to the list        ScheduleTask *task = new ScheduleTask(initial_delay, repeat_interval,ntimes);                schedulelist->addToFront(task);		//schedulelist->addElement(task);		        args->task = task;        args->function = funptr;        args->argument = arg;                                // spawn the thread        pthread_create(task->getThread(), NULL, scheduledExecutionThread, args);                // make sure that the spawned has time to get a hold of the primary mutex        // otherwise, the scheduler can decide that the task has finished before        // it gets a chance to fire for the first time. Once the mutex is held, however,        // this race condition is avoided.        //pthread_mutex_lock(task->getPrimedMutex());        //pthread_cond_wait(task->getPrimedCondition(), task->getPrimedMutex());        //pthread_mutex_unlock(task->getPrimedMutex());                task->setPriority();        task->activate();                return task;}void StandardScheduler::tick(){        pthread_cond_broadcast(&direct_tick_condition);		long right_now = getCurrentTime();		//int nelements = schedulelist->getNElements();		        //for(int i = 0; i < nelements; i++){        //        (schedulelist->getElement(i))->tick(right_now);        //}				ScheduleTask *node = (ScheduleTask *)schedulelist->getFrontmost();		while(node != NULL){			node->tick(right_now);			node = (ScheduleTask *)node->getNext();		}		}void StandardScheduler::tick(long time){		ScheduleTask *node = (ScheduleTask *)schedulelist->getFrontmost();		while(node != NULL){			node->tick(time);			node = (ScheduleTask *)node->getNext();		}}pthread_cond_t *StandardScheduler::getDirectTickCondition(){        return &direct_tick_condition;}/// PLUGIN ENTRY POINTPlugin *getPlugin(){    return new StandardSchedulerPlugin();}