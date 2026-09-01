# Multi queue priority Scheduler
TODO: allow tasks to have a higher initial priority only if the caller is the kernel

TODO: once multiple cores are added, figure out a way to do load balancing between schedulers

## High Prio Program
- I/O bound
- security related
- important for to the OS or kernel to function properly
- driver/interrupt handler
- interactive/user-facing
- real-time

## Low Prio Program
- CPU bound
- background daemon
- long running batch job (rendering, indexing)

## Design
- the scheduler has 10 queues. tasks in a higher queue will get chosen over tasks in lower queues
- the scheduler has a special queue, the sleeping queue. all queues that voluntarilly choose to relinquish the CPU
by sleeping are put inside this thread
- the scheduler does NOT have a special queue for waiting threads. it is the responsibility of the resource the thread is waiting on to notify the waiting thread
- inside each queue, the next task will be chosen according to round robin logic
- every x clock interrupts that a task starves will prompt the task to promote 1 level up
- each task can promote to at most the 8th queue
- tasks in the 9th or 10th queue will never move to another queue
- a task that uses up its entire CPU quantum will get demoted by 1 level
- a task that uses up less that 40% of its CPU quantum will be promoted by 1 level
- each task will remember its base queue level (starving tasks will NOT increase their base queue level. tasks change their queue level for any other reason will also modify their base queue level), so that it can reinserted into its queue
- a starving task will return to its initial queue once it has finished running (if any other promoting/demoting rules do not apply)

## Behaviour
- every clock tick, the scheduler will check if the current process can be taken off the CPU (quantum expired, process blocked or terminated). if so, it will insert the process into its queue and pick a new process
- on every clock tick, if the currently running tasks is not important enough (in the 10th queue. or 9th if the 10th queue is not empty), the scheduler will scan the 9th and 10th queue. if any tasks are ready to be schedule inside of those queues, the currently running tasks is evicted in favour of this newly ready tasks.
- every nth clock ticks, the scheduler will scan the lower queues to see if any process have been starving for too long. only threads in the ready state are checked to see if they are starving
- every nth clock ticks, the scheduler will scan the sleeping queue and wake up all threads that can be woken up

ISSUE: this scan for starving threads will at worst be O(n) (but at best, O(1)). This can lead to issues if there a lot of tasks to be scheduled. TODO: review this part of the scheduler behaviour if it proves to slow down scheduling too much. an event driven approach is the only alternative that i see to this issue
ISSUE: currently, waiting threads stay in the same queue as ready threads. TODO: analyze this part of the scheduler and decide whether it is too costly or not
