typedef struct process {
	struct process *next;       /* next process in pipeline */
	char **argv;                /* for exec */
	pid_t pid;                  /* process ID */
} process;


