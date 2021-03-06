#include <types.h>
#include <lib.h>
#include <thread.h>
#include <syscall.h>
#include <synch.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <current.h>
#include <copyinout.h>
#include <kern/wait.h>

void
sys__exit(int exitcode){
	struct pid_list *tmp;
	int i;

	for (i=0; i<MAX_FILE_DESCRIPTOR; i++){
		sys_close(i);
	}
	while(curthread->children != NULL) {
		tmp = curthread->children;
		process_table[tmp->pid]->parent_pid = -1; // Mark children as orphans
		curthread->children = curthread->children->next;
		kfree(tmp);
	}
	curthread->exit_status = _MKWAIT_EXIT(exitcode);

	thread_exit();
}

pid_t
sys_waitpid(pid_t pid, int *status, int options, int *err){
	if (options != 0){
		*err = EINVAL;
		return -1;		
	}
	if (status == NULL){
		*err = EFAULT;
		return -1;
	}
	if (pid < PID_MIN || pid > PID_MAX){
		*err = ESRCH;
		return -1;
	}
	if (process_table[pid] == NULL) {
		*err = ESRCH;
		return -1;
	}

	int contains = 0;
	struct pid_list *tmp = curthread->children;
	while (tmp != NULL){
		if (tmp->pid == pid){
			contains = 1;
			break;
		}
		tmp = tmp->next;
	}
	if (!contains){
		*err = ECHILD;
		return -1;
	}
	// The child will only V this semaphore in thread_exit with interrupts off, just before it switches to zombie.
	// Thus, the parent will only proceed after a child has completely exited
	P(process_table[pid]->waiting_on);

	// Remove pid from list of children
	// Check head of list
	struct pid_list *curr = curthread->children;
	if (curr->pid == pid){
		curthread->children = curr->next;
		kfree(curr);
	}
	else{
		while (curr->next != NULL){
			if (curr->next->pid == pid){
				tmp = curr->next;
				curr->next = curr->next->next;
				kfree(tmp);
				break;
			}
			curr = curr->next;
		}
	}

	sem_destroy(process_table[pid]->waiting_on);
	*err = copyout(&(process_table[pid]->exit_status), (userptr_t)status,sizeof(int));	
	process_table[pid]->parent_pid = -1; // Mark for reaping by exorcise
	process_table[pid] = NULL;

	return pid;
}

pid_t
sys_getpid(void){
	return curthread->pid;
}
