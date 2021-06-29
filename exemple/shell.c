#define _POSIX_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
//#define _BSD_SOURCE 
#define _XOPEN_SOURCE 500     /* or any value > 500 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>

#define WAIT_ANY -1

pid_t shell_pgid;
int shell_terminal;
int shell_is_interactive;
struct termios shell_tmodes;

/* --------- Initialisation du shell ------------------*/

	/* On doit s'assurer que le shell est interractive 
        et en arrière plan */

void init_shell () {     
    //On regarde si il est interractive   
	shell_terminal = STDIN_FILENO;
	shell_is_interactive = isatty (shell_terminal);

	if (shell_is_interactive) {
		// Boucle jusqu'à ce que notre shell soit en arrière plan  
      // tcgetpgrp : permet de recuperer un ID que l'on compare avec celui du premier plan actuel */
      while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
			kill (- shell_pgid, SIGTTIN);

		/* Ignorer les signaux interactifs et de contrôle du travail
        	Pour ne pas s'arrerter par accident  */
		signal (SIGQUIT, SIG_IGN);
		signal (SIGTSTP, SIG_IGN);
		signal (SIGTTIN, SIG_IGN);
		signal (SIGTTOU, SIG_IGN);
		signal (SIGCHLD, SIG_IGN);

		// Se placer dans son propre groupe de processus 
		shell_pgid = getpid();
		// setpgid : Définir l'ID du groupe de processus pour le contrôle des travaux
		if (setpgid (shell_pgid, shell_pgid) < 0)
			{
          perror ("Couldn't put the shell in its own process group");
          exit (1);
        }
		// On prend le contrôle du terminal. 
		tcsetpgrp(shell_terminal, shell_pgid);
		//Sauvegarder les attributs de terminal par défaut pour le shell. 
		tcgetattr (shell_terminal, &shell_tmodes);
	}	
}

/*      Nos Structure de données      */
	/* Processus   */

typedef struct process {
	struct process *next;       /* pointeur vers le prochain processus  */
	char **argv;                /* pour executer  */
	pid_t pid;                  /* process ID */
} process;
     
     /* Job - Liste de processus   */
typedef struct job {
	struct job *next;           /* pointeur vers le prochain job */
	process *first_process;     /* pointeur vers les processus du job - nottament le 1er */
	pid_t pgid;                 /* process group ID */
	struct termios tmodes;      /* saved terminal modes */
	char * input;		 /* file i/o channels */
	char * output; 
} job;

/* On initialise le job - pour le moment il ne pointe rien */   
job *first_job = NULL;
/*  Initializing a job */
job * job_initialize (char **argv, int  num_tokens, int *foreground) {
	job * j; 			// on créer un pointeur vers un job
	process * p;	 	// on créer un pointeur vers un process
	char ** command;	
	int i, counter,test;
	
	/* Initialisation du Job */
	j = (job *) malloc (sizeof(job)); // espace memoire pour le job
	j->first_process = NULL;
	j->input = NULL;
	j->output = NULL;
	command = (char **) malloc (sizeof(char **) * (num_tokens + 1));
	
	/*	Vérifie si l'argument doit être exécuté en arrière-plan */
	if (strcmp(argv[num_tokens - 1], "&") == 0) {
		*foreground = 0;
		num_tokens--;
	}
	else 
		*foreground = 1;

	/*	Vérifier l'entrée entrante analysée pour le pipe et/ou la redirection. */
	counter = 0;		//  Utilisé pour garder la trace des jetons de commande individuels
	for ( i = 0; i < num_tokens; i++) {		
		if(strcmp(argv[i],  "<") == 0) {
			if((j->first_process)  || (num_tokens <= i + 1 )) {
				printf("ERROR: Unable to redirect files in this manner\n");
				return NULL;
			}			
			j->input = argv[++i];
			if (num_tokens == i + 1) {
				j->first_process = (process *) malloc (sizeof(process));
				j->first_process->argv = (char ** ) malloc (counter * 100);
				for ( test = 0; test < counter; test++) 
					j->first_process->argv[test] = command[test];
				j->first_process->argv[test] = '\0';
				j->first_process->next = NULL;
				return j;
			}
		}
		else if(strcmp(argv[i],  ">") == 0) {
			if ( i + 2 == num_tokens){		//  There was a token specified for redirection
				j->output = argv[i + 1];
				command[counter] = '\0';
				if (!j->first_process) {
					j->first_process = (process *) malloc (sizeof(process));
					j->first_process->argv = command;
				}
				else {
					p = j->first_process; 
					while (p->next)
						p = p->next;
					p->next = (process *) malloc (sizeof(process));
					p = p->next;
					p->next = NULL;
					p->argv = command;
				} 
				return j;
			}
			else {
				printf("ERROR: Incorrect specification of files\n");
				return NULL;
			}
		} 
		else
			command[counter++] = argv[i]; // on rempli command avec les arguments entree en param
	}
	command[counter] = '\0';
	if (!j->first_process) {
		j->first_process = (process *) malloc (sizeof(process));
		j->first_process->argv = command;
	}
	else {
		p = j->first_process; 
		while (p->next)
			p = p->next;
		p->next = (process *) malloc (sizeof(process));
		p->next->argv = command;
	} 
	return j;
}


void  parse(char *line, char **argv, int  *tokens) {
	*tokens = 0;
	while (*line != '\0') {       /* Si ce n'est pas la fin de la ligne....... */ 

		while (isspace(*line) || iscntrl(*line)) {
			if (*line == '\0') {
				*argv = '\0';			
				return;
			}
			*line++ = '\0';     /*remplacer les espaces blancs par '\0'  */
		}
	         /* sauvegarder la position de l'argument      */ 
		if(*line != '<' && *line != '>') {
			*argv++ = line; 
			(*tokens)++;
		}
		while (isalnum(*line) || ispunct(*line)) { // si c'est un caractere alphanumerique ou ponctuation
			if (*line == '<') {
				(*tokens)++;
				*line++ = '\0';
				*argv++ = "<";
				break;
			}
			else if (*line == '>') {
				(*tokens)++;
				*line++ = '\0';
				*argv++ = ">";
				break;
			}
			
			else
				line++;             /* on saute l'argument   */
		}
	}
	*argv = '\0';                 /* on marque la fin  */
}


void put_job_in_foreground (job *j) {
       /* Put the job into the foreground.  */
	tcsetpgrp (shell_terminal, j->pgid);
   
       /* Wait for it to report.  */
	waitpid (WAIT_ANY, 0, WUNTRACED);

       /* Put the shell back in the foreground.  */
	tcsetpgrp (shell_terminal, shell_pgid);
       /* Restore the shell's terminal modes.  */
	tcgetattr (shell_terminal, &j->tmodes);
	tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);
}


    /* Store the status of the process pid that was returned by waitpid.
        Return 0 if all went well, nonzero otherwise.  */	

void launch_job (job *j, int foreground) {
	process *p;
	pid_t pid;
	int mypipe[2], infile, outfile;
    
  	if (j->input){
  		if((infile = open(j->input, O_RDONLY))< 0) {
  			printf("ERROR: Could not open read file\n");
			return;
		}
	}
  	else
		infile = STDIN_FILENO;
	for (p = j->first_process; p; p = p->next) {
           /* Set up pipes, if necessary.  */
		if (p->next){
			if (pipe (mypipe) < 0) {  /*	If pipe fails */
				printf("ERROR: Unable to pipe input\n");
				return;
			}
			outfile = mypipe[1];
		}
		else if (j->output) {
			outfile = open(j->output, O_RDWR | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
		}
		else
			outfile = STDOUT_FILENO;
           /* Fork the child processes.  */
		pid = fork ();
		if (pid == 0) { // Equivalent à launch process
     	       /* This is the child process.  */
			if (shell_is_interactive) {
				signal (SIGINT, SIG_DFL);
				signal (SIGQUIT, SIG_DFL);
				signal (SIGTSTP, SIG_DFL);
				signal (SIGTTIN, SIG_DFL);
				signal (SIGTTOU, SIG_DFL);
				signal (SIGCHLD, SIG_DFL);					
				pid = getpid ();
				if (j->pgid == 0) 
					j->pgid = pid;
				setpgid (pid, j->pgid);
				if (foreground)
					tcsetpgrp (shell_terminal, j->pgid);			
			}

			/* Gestion des entree sortie */
			if (infile != STDIN_FILENO) {
				if(close(STDIN_FILENO) < 0) 
					printf("ERROR: Could not close STDIN\n");
				if(dup(infile) != STDIN_FILENO)
					printf("ERROR: Could not dup infile\n");
			}
			if (outfile != STDOUT_FILENO) {
				if (close(STDOUT_FILENO) < 0)
					printf("ERROR: Could not close STDOUT\n");			
				if (dup (outfile) != STDOUT_FILENO)
					printf("ERROR: dup outfile\n");
			}	
			if (execvp (p->argv[0], p->argv) < 0) 
				printf("ERROR: Could not execute command\n");
			exit (1);
		}
		else if (pid < 0) {
     	          /* The fork failed.  */
			printf("ERROR: forking child process failed\n");
			exit (1);
		}
		else {
     	         /*  This is the parent process.  */ 	      
			p->pid = pid;
			if (shell_is_interactive) {
				if (!j->pgid)
	         			j->pgid = pid;
         			setpgid (pid, j->pgid); 	    
			}
		}
	if (infile != STDIN_FILENO)
		close(infile);
	if (outfile != STDOUT_FILENO)
		close(outfile);
	infile = mypipe[0];
	}
	if (foreground)
		put_job_in_foreground(j);
}

      /*    Delete terminated jobs from the active job list.  */
void free_job(job * j) {
	free (j);
}

 /* -------- Nos fonctions CD et CP ----------*/
 
//Fonction pour copier un fichier
void cpfile(const char *src , const char *dest){

    /*struct stat st;      
    fstat (fsrc =, &st);  sans chmod */
    int fsrc = open(src, O_RDONLY); // on ouvre en lecture seulement 
    int fdest = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0666);

    struct stat istat;
    fstat(fsrc, &istat);
    fchmod(fdest, istat.st_mode); // passer par chmod
    
    while(1){
        char buffer[4096];
        int rcnt = read(fsrc, buffer, sizeof(buffer));
        if (rcnt == 0)
            break;
        while (rcnt != 0){
        int pos = 0;
            int wcnt = write (fdest, buffer + pos, rcnt); 
            rcnt -= wcnt; // On enleve ce que l'on a écrit
            pos += wcnt; // on reprend l'ecriture la ou on c'est arrété 
        }
    }
    close(fdest);
    close(fsrc);
    return;

}

//Fonction pour copier un repertoire
void cprep(const char *src , const char *dest){
	
	// Copie de repertoire
	DIR* fsrc = opendir(src);
    DIR* fdest = opendir(dest);

    if (fdest == NULL) { // Si le fichier destination n'existe pas 
        mkdir(dest, 0777); // On le crée
    }
	struct dirent *pd ;


    pd = readdir(fsrc);

    struct stat info;

    while( (pd = readdir(fsrc)) != NULL) // Tant qu'il y a des éléments à copier
	{
        char path_src[100];
        char path_dest[100];
        char filename[100];

        if(strncmp(pd->d_name,".",1) == 0)
			continue ;
        
        else {
          strcpy(path_dest,dest); // On copie dans path_dest le chemin de dest 
          strcpy(path_src,src); // On copie dans path_src le chemin de src 

          strcpy(filename, pd->d_name); //on recupere le nom du fichier sur lequel pointe pd
          strcat(path_dest,"/");   // On fais la même chose pour path_dest 
        	strcat(path_dest,filename);
          strcat(path_src,filename); // on lui ajoute ensuite le nom du fichier pointe
          strcat(path_src,"/"); // on rajoute / au chemin path_src
          
			    stat(path_src,&info); //on recupere les infos du fichier 

			if(S_ISDIR(info.st_mode)!=0){ // si c'est un repertoire
			//if(pd->d_type == DT_DIR){ // si c'est un repertoire avec autre methode
          mkdir(path_dest, 0777); // On crée le fichier à l'emplacement path_dest 
				  cprep(path_src,path_dest); // on copie les fichiers à l'interieur du repertoire de maniere recursive 
			}
			else { // si c'est un fichier : 
        cpfile(path_src,path_dest); // on reutilise la fonction de l'etape 2 
			  }	
      }   
  }
    
    closedir(fsrc);
    closedir(fdest);
    return;
}


// Fonction de copie générale
void cp(const char *src , const char *dest){
    
    struct stat info;

    stat(src,&info); //on recupere les infos du fichier 

    if(S_ISDIR(info.st_mode)!=0){   // si c'est un répertoire on utilise cpdir
        cprep(src,dest);
    }
    else {
        cpfile(src, dest);          // Sinon on utilise cpfile
    }

    return;
}
// La fonction cd pour atteindre un dossier
void cd (char * dir) {
	char path[100];
	
	if (dir != NULL) {
		getcwd(path, sizeof(path));     //  Grabs current working directory
		strncat(path, "/", strlen(path));
		strncat(path, dir, strlen(dir));		//  append desired change in directory to cwd
		if (chdir(path) < 0)		//  Changes cwd and checks for failure
			printf("ERROR: No such file or directory %s\n", dir);			
		return;		
	}
}

void printChemin() { // affiche le chemin actuelle 
  char chemin[1024];
  getcwd(chemin, sizeof(chemin));
  
  printf("%s", chemin);
}

/* -------- HELP -------- */
void help(){
  printf(" \t\tSHELL Polytech Paris Saclay \n");
  printf("\t\t-----------------------------\n");
  printf("\t\tréalisé par Natanael et Bilail\n\n\n");
  printf("Liste des commandes : \n"
    "- cd [chemin]\n"
    "- ls\n"
    "- cp [source] [destination] \n"
    "- exit\n"
    "- help\n ");
}

int  main(int argc, char ** argvFILE) {
	char  line[1024];             /* the input line                 */
	char  *argv[64];              /* the command line argument      */
	char * p;
	int tokens, foreground;
	int * ptokens =&tokens;
	int * pforeground =&foreground;

	int input_from_file = ftell(stdin);		/* on regarde si l'entrée vient d'un fichier */
	

	
  printf("\n\t\t-----------------------------\n");
  printf(" \t\tSHELL Polytech Paris Saclay \n");
  printf("\t\t-----------------------------\n");
  printf(" _____   _____   _      __    __  _____   _____   _____   _   _ \n" 
"|  _  \\ /  _  \\ | |     \\ \\  / / |_   _| | ____| /  ___| | | | | \n"
"| |_| | | | | | | |      \\ \\/ /    | |   | |__   | |     | |_| | \n"
"|  ___/ | | | | | |       \\  /     | |   |  __|  | |     |  _  | \n"
"| |     | |_| | | |___    / /      | |   | |___  | |___  | | | | \n"
"|_|     \\_____/ |_____|  /_/       |_|   |_____| \\_____| |_| |_| \n");
  printf("\n\t\tréalisé par Natanael et Bilail\n\n");
  printf("\t\t==============================\n");



	init_shell();	
	while (1) {                   /* repeat until done ....         */
		if(input_from_file < 0){	/*	stdin is coming from user not file */
			printChemin();
			printf(" - Polytech Paris Saclay >");        /*   display a prompt             */   	             	
		
			memset (line, '\0', sizeof(line));		// zero line, (fills array with null terminator)
       		memset (argv, '\0', sizeof(argv));

        	if (!fgets(line, sizeof(line), stdin)) 	{printf("\n"); return 0;} }	// Exit upon ctrl-D
     		if(strlen(line) == 1) { continue;}	//	 check for empty string aka 'enter' is pressed without input
	
        if ((p = strchr(line, '\n')) != NULL)	//	remove '\n' from the end of 'line'
			*p = '\0';
		parse (line, argv, ptokens);		// parse input to shell
		if (argv[0] == NULL)
			continue;

		// On regarde quelle commande a été entrée : 

		else if (strcmp(argv[0], "exit") == 0)  //   on sort du programme      
			return 0;                                  
		else if (strcmp(argv[0], "cd") == 0) // on execute la fonction cd
			cd (argv[1]);
		else if (strcmp(argv[0], "cp") == 0) // on execute la fonction cp
			cp(argv[1], argv[2]);
		else if (strcmp(argv[0], "help") == 0) // on execute la fonction cp
			help(argv[1]);	

		// Sinon c'est que l'on execute un programme             	
		else {
			if ((first_job = job_initialize(argv, tokens, pforeground)) != NULL) {
				launch_job(first_job, foreground);
				free_job(first_job);
			}
		}
	}
}
