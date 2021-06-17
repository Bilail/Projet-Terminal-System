/* A job is a pipeline of processes.  */

typedef struct job {
	struct job *next;           /* next active job */
	process *first_process;     /* list of processes in this job */
	pid_t pgid;                 /* process group ID */
	struct termios tmodes;      /* saved terminal modes */
	char * input;		 /* file i/o channels */
	char * output; 
} job;
