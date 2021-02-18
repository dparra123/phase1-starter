#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <usloss.h>
#include <phase1Int.h>

#define CHECKKERNEL() \
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) USLOSS_IllegalInstruction()

#define FREE 0
#define BUSY 1

typedef struct ProcNode {
    int                 pid;            // processes id
    struct ProcNode     *next;           // next node
} ProcNode;

typedef struct ProcQueue {
    struct ProcNode *head, *tail;
} ProcQueue;

typedef struct Lock
{
    int                 inuse;
    char                name[P1_MAXNAME];
    int                 state; // BUSY or FREE
    int                 pid;
    int                 lid;
    struct ProcQueue    queue;              // head of blocked processes linked list
    // more fields here
} Lock;


static Lock locks[P1_MAXLOCKS];

/*
*  enqueue adds a node to the tail of a queue struct
*  Params:
*      @lid lock id of lock's queue we wish to add to
*      @pid process id of process we wish to enqueue
*/
void enqueue(int lid, int pid) {
    ProcNode *temp = (struct ProcNode*)malloc(sizeof(struct ProcNode));
    temp->pid = pid;
    // If queue is empty
    if (locks[lid].queue.head == NULL) {
        locks[lid].queue.head = temp;
        locks[lid].queue.tail = temp;
        temp->next = NULL;
    } else {
        locks[lid].queue.tail->next = temp;
        locks[lid].queue.tail = temp;
    }
}

/*
 *  dequeue returns the pid of the head of a queue
 *  Params:
 *      @lid lock id we wish to dequeue from
 *  Returns:
 *      @pid process id of process that we dequeued
 */
int dequeue(int lid) {
    int pid;
    // if linked is empty return -1
    if (locks[lid].queue.head == NULL) return -1;
    // else grab head
    ProcNode *temp = locks[lid].queue.head;
    pid = temp->pid;
    locks[lid].queue.head = temp->next;
    // free head
    free(temp);
    // return process id of dequeued node
    return pid;
}

void 
P1LockInit(void) 
{
    CHECKKERNEL();
    P1ProcInit();
    for (int i = 0; i < P1_MAXLOCKS; i++) {
        locks[i].inuse = FALSE;
        locks[i].state = FREE;
        locks[i].lid = -1;
        locks[i].pid = -1;
    }
}

int CheckDupName(char *name) {
    for (int i = 0; i < P1_MAXLOCKS; i++) {
        if (strcmp(locks[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int P1_LockCreate(char *name, int *lid)
{
    int result = P1_SUCCESS;
    CHECKKERNEL();
    // disable interrupts
    int dis = P1DisableInterrupts();
    // check parameters
    if (name == NULL) result = P1_NAME_IS_NULL;
    else if (strlen(name) > P1_MAXNAME) result = P1_NAME_TOO_LONG;
    else if (CheckDupName(name)) result = P1_DUPLICATE_NAME;
    else {
        // find a free Lock and initialize it
        int lockFound = 0;
        for (int i = 0; i < P1_MAXLOCKS; i++) {
            if (locks[i].lid == -1) {
                lockFound = 1;
                *lid = i;
                locks[i].lid = i;
                strcpy(locks[i].name, name);
                break;
            }
        }
        if (!lockFound) {
            result = P1_TOO_MANY_LOCKS;
        }
    }
    // re-enable interrupts if they were previously enabled
    if (dis) P1EnableInterrupts();
    return result;
}

int P1_LockFree(int lid) 
{
    int     result = P1_SUCCESS;
    CHECKKERNEL();
    // disable interrupts
    int dis = P1DisableInterrupts();
    // check if any processes are waiting on lock
    if (locks[lid].queue.head != NULL) result = P1_BLOCKED_PROCESSES;
    else if (lid < 0 || lid > P1_MAXLOCKS) result = P1_INVALID_LOCK;
    else {
        // mark lock as unused and clean up any state
        locks[lid].inuse = FALSE;
        locks[lid].state = FREE;
        locks[lid].pid = -1;
        memset(locks[lid].name, 0, P1_MAXNAME);
    }
    // restore interrupts
    if (dis) P1EnableInterrupts();
    return result;
}

// May not be right, not 100% on this one
int P1_Lock(int lid) 
{
    int result = P1_SUCCESS;
    CHECKKERNEL();
    int dis = P1DisableInterrupts();
    if (lid < 0 || lid > P1_MAXLOCKS) result = P1_INVALID_LOCK;
    else {
        /*********************
        Pseudo-code from the lecture notes.
        while(1) {
              DisableInterrupts();
              if (lock->state == FREE) {
                  lock->state = BUSY;
                  break;
              }
              Mark process BLOCKED, add to lock->q
              RestoreInterrupts();
              Dispatcher();
        }
        RestoreInterrupts();
        *********************/
        // THIS PART SPECIFICALLY I AM NOT SURE IN MYSELF

        while(1) {
            int pid = P1_GetPid();
            if (locks[lid].state == FREE) {
                locks[lid].state = BUSY;
                locks[lid].pid = pid;
                break;
            }
            P1SetState(pid, P1_STATE_BLOCKED, lid, 0);
            enqueue(lid, pid);
            if (dis) P1EnableInterrupts();
            P1Dispatch(FALSE);
        }

    }

    if (dis) P1EnableInterrupts();
    return result;
}


int P1_Unlock(int lid) 
{
    int result = P1_SUCCESS;
    CHECKKERNEL();
    int dis = P1DisableInterrupts();
    int currProcess = P1_GetPid();
    if (currProcess != locks[lid].pid) result = P1_LOCK_NOT_HELD;
    else if (lid < 0 || lid > P1_MAXLOCKS) result = P1_INVALID_LOCK;
    else {
        /*********************
         DisableInterrupts();
         lock->state = FREE;
         if (lock->q is not empty) {
            Remove process from lock->q, mark READY
            Dispatcher();
         }
         RestoreInterrupts();
         *********************/
        locks[lid].state = FREE;
        if (locks[lid].queue.head != NULL) {
            int pid = dequeue(lid);
            P1SetState(pid, P1_STATE_READY, lid, 0);
            P1Dispatch(FALSE);
        }
    }
    if (dis) P1EnableInterrupts();
    return result;
}

int P1_LockName(int lid, char *name, int len) {
    int result = P1_SUCCESS;
    CHECKKERNEL();
    // more code here
    if (lid < 0 || lid > P1_MAXLOCKS) result = P1_INVALID_LOCK;
    else if (locks[lid].name == NULL) result = P1_NAME_IS_NULL;
    else {
        for (int i = 0; i < len; i++) {
            name[i] = locks[lid].name[i];
        }
    }
    return result;
}

/*
 * Condition variable functions.
 */

typedef struct Condition
{
    int         inuse;
    char        name[P1_MAXNAME];
    int         lock;  // lock associated with this variable
	int			vid;
	struct ProcQueue    queue;
	int			waiting;
	int			state;
    // more fields here
} Condition;

static Condition conditions[P1_MAXCONDS];

void P1CondInit(void) {
    CHECKKERNEL();
    P1LockInit();
    for (int i = 0; i < P1_MAXCONDS; i++) {
        conditions[i].inuse = FALSE;
		conditions[i].lock = -1;
		conditions[i].vid = -1;
		conditions[i].waiting = 0;
		conditions[i].state = FREE;
    }
}

int P1_CondCreate(char *name, int lid, int *vid) {
    int result = P1_SUCCESS;
	int free = -1;
	int i;
	int dis = DisableInterrupts();
    CHECKKERNEL();
    if (name == NULL){
		result = P1_NAME_IS_NULL;
	} else if (strlen(name) > P1_MAXNAME) {
		result = P1_NAME_TOO_LONG;
	} else if (lid > P1_MAXLOCKS || lid < 0 || locks[lid].lid = -1) {
		result = P1_INVALID_LOCK;
	} else {
		for (i = 0; i < P1_MAXCONDS; i++) {
			if (conditions[i].vid = -1 && free == -1) {
				free = i;
			}
			if (strcmp(conditions[i].name, name)) {
				return P1_DUPLICATE_NAME;
			}
		}
		if (free == -1) {
			return P1_TOO_MANY_CONDS;
		}
		conditions[free].lock = lid;
		conditions[free].vid = free;
		strcpy(conditions[free].name, name);
		*vid = free;
	}
	if (dis) EnableInterrupts();
    return result;
}

int P1_CondFree(int vid) {
    int result = P1_SUCCESS;
    CHECKKERNEL();
	int dis = DisableInterrupts();
	if (conditions[vid].vid == -1 || vid < 0 || vid > P1_MAXCONDS) {
		result = P1_INVALID_COND;
	} else if (conditions[vid].queue.head != NULL || conditions[vid].waiting > 0) {
		result = P1_BLOCKED_PROCESSES;
	} else {
		conditions[vid].vid = -1;
		conditions[vid].lock = -1;
		strcpy(conditions[vid].name, "");
		conditions[vid].state = FREE;
		conditions[vid].waiting = 0;
	}
	if (dis) EnableInterrupts();
    return result;
}


int P1_Wait(int vid) {
    int result = P1_SUCCESS;
    CHECKKERNEL();
    /*********************
      DisableInterrupts();
      Confirm lock is held
      cv->waiting++;
      Release(cv->lock);
      Make process BLOCKED, add to cv->q
      Dispatcher();
      Acquire(cv->lock);
      RestoreInterrupts();
    *********************/
    return result;
}

int P1_Signal(int vid) {
    int result = P1_SUCCESS;
        CHECKKERNEL();
    /*********************
      DisableInterrupts();
      Confirm lock is held 
      if (cv->waiting > 0) {
        Remove process from cv->q, make READY
        cv->waiting--;
        Dispatcher();
      }
      RestoreInterrupts();
    *********************/    
    return result;
}

int P1_Broadcast(int vid) {
    int result = P1_SUCCESS;
    CHECKKERNEL();
    /*********************
      DisableInterrupts();
      Confirm lock is held 
      while (cv->waiting > 0) {
        Remove process from cv->q, make READY
        cv->waiting--;
        Dispatcher();
      }
      RestoreInterrupts();
    *********************/    
    return result;
}

int P1_NakedSignal(int vid) {
    int result = P1_SUCCESS;
    CHECKKERNEL();
    // more code here
    return result;
}

int P1_CondName(int vid, char *name, int len) {
    int result = P1_SUCCESS;
    CHECKKERNEL();
    // more code here
    return result;
}
