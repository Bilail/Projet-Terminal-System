#define _POSIX_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
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

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>

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
	int status;                 /* reported status value */
	char stopped;               /* true si le process est  stopped */
	char completed;             /* true si le process est completed */
} process;
     
     /* Job - Liste de processus   */
typedef struct job {
	struct job *next;           /* pointeur vers le prochain job */
	process *first_process;     /* pointeur vers les processus du job - nottament le 1er */
	pid_t pgid;                 /* process group ID */
	struct termios tmodes;      /* saved terminal modes */
	char * input;		 /* file i/o channels */
	char * output;
	int stdin, stdout, stderr; 
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
		if (strcmp(argv[i], "|") == 0) {
			if (!j->first_process) { // si il n'y a pas de processus 
				j->first_process = (process *) malloc (sizeof(process)); // on en crée un 
				j->first_process->argv = (char ** ) malloc (counter * 100); // on lui crée  un argv
				for ( test = 0; test < counter; test++) 
					j->first_process->argv[test] = command[test]; // on rempli argv
				j->first_process->argv[test] = '\0';
				j->first_process->next = NULL;
			}	
			else {
				p = j->first_process; // SI il y a deja des process
				while (p->next)
					p = p->next; // on rajout eun process à la fin
				p->next = (process *) malloc (sizeof(process)); // on initialise le process
				p->next->argv = (char ** ) malloc (counter * 100);
				p = p->next;
				for ( test = 0; test < counter; test++) 
					p->argv[test] = command[test];
				p->argv[test] = '\0';
				p->next = NULL;				
			}
				//--Effacer les données stockées dans la commande et commencer à stocker
			memset(command, '\0', sizeof(char**) * num_tokens);	
			counter = 0;
		}		
		else if(strcmp(argv[i],  "<") == 0) {
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
		if(*line != '<' && *line != '>' && *line != '|') {
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
			else if (*line == '|') {
				(*tokens)++;
				*line++ = '\0';
				*argv++ = "|";
				break;
			}
			else
				line++;             /* on saute l'argument   */
		}
	}
	*argv = '\0';                 /* on marque la fin  */
}


/* Return 1 si tous les process du job qu el'on a donné en paramètre sont fini (complétés)  */
int job_is_completed (job *j)
{
  process *p;
    // On parcours tous les process du job 
  for (p = j->first_process; p; p = p->next)
    if (!p->completed) // Si l'un n'est pas fini 
      return 0;       // On retourne 0
  return 1;          // Sinon on retourne 1
}


/* Return 1 si tous les process du job qu'on donne en paramètre sont stoppé ou fini   */
int job_is_stopped (job *j)
{
  process *p;
// On parcours tous les process du job 
  for (p = j->first_process; p; p = p->next)
    if (!p->completed && !p->stopped) // Si le job n'est pas complété ou stoppe 
      return 0;                      // On retourne 0
  return 1;                         // Sinon on retourne 1 
}

/* Fonction stockant le status du processus identifié par pid retourné par waitpid,
si tout s'est bien passé, retourne 0, et -1 sinon  */

int mark_process_status (pid_t pid, int status)
{
  job *j;                   // le job auquel appartient le processus
  process *p;               // le processus 

  if (pid > 0)              // si le processus n'est pas un swapper
    {
      /* Update the record for the process.  */
      for (j = first_job; j; j = j->next)                   // Parcours des jobs
        for (p = j->first_process; p; p = p->next)          // Parcours des processus
          if (p->pid == pid)                                // On trouve le processus correspondant
            {
              p->status = status;                           // On initialise son statut
              if (WIFSTOPPED (status))                      // Si le processus a été arreté par un signal
                p->stopped = 1;                             // mise à jour de son champ stopped
              else
                {
                  p->completed = 1;                                      // Sinon màj de son champ completed
                  if (WIFSIGNALED (status))                             // S'il s'est terminé à cause d'un signal
                    fprintf (stderr, "%d: Terminated by signal %d.\n",
                             (int) pid, WTERMSIG (p->status));
                }
              return 0;                                         // status du processus bien stocké, pas d'erreur, on retourne 0
             }
      fprintf (stderr, "No child process %d.\n", pid);
      return -1;
    }
  else if (pid == 0 || errno == ECHILD)
    /* No processes ready to report.  */
    return -1;
  else {
    /* Other weird errors.  */
    perror ("waitpid");
    return -1;
  }
}


/* Vérifie si il y as des processus dont l'information sur l'état est disponible,
   bloque jusqu'à ce que tous les processus du job concerné soit arretés ou terminés  */
void wait_for_job (job *j)
{
  int status;
  pid_t pid;

  do
    pid = waitpid (WAIT_ANY, &status, WUNTRACED);             // suspendre l'éxecution
  while (!mark_process_status (pid, status)                   // tant qu'il y a des processus dont le statut n'est pas mis à jour,
         && !job_is_stopped (j)                               // et que tous les processus du job ne sont pas arretés ou terminés
         && !job_is_completed (j));
}


/* -------- Premier et Arrière plan ----------*/

void put_job_in_foreground (job *j, int cont)
{
  /* Mettre le job au premier plan */
  /* tcgetpgrp : permet de recuperer un ID que l'on compare avec celui du premier plan actuel 
  On lui donne le controle sur le shell*/
  tcsetpgrp (shell_terminal, j->pgid);

  /* Send the job a continue signal, if necessary.  */
  if (cont)
    {
      tcsetattr (shell_terminal, TCSADRAIN, &j->tmodes);
      if (kill (- j->pgid, SIGCONT) < 0)
        perror ("kill (SIGCONT)");
    }

  /* On attend que tous les process du job se termine  */
  wait_for_job (j);

  /* Un fois fini on remet le shell en premier plan   */
  tcsetpgrp (shell_terminal, shell_pgid);

  /* On restaures les modes du shell, si ils avaient été modfié par les process  */
  tcgetattr (shell_terminal, &j->tmodes);
  tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);
}

/* Mettre le job à l'arriere pla, */
void put_job_in_background (job *j, int cont)
{
  /* Send the job a continue signal, if necessary.  */
  if (cont)
    if (kill (-j->pgid, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
}

/* Lancement d'un process */
void launch_process (process *p, pid_t pgid,int infile, int outfile,int errfile,int foreground) {
  pid_t pid;

  if (shell_is_interactive)
    {
      /* Placez le processus dans le groupe de processus et donnez au groupe de processus
         le terminal, le cas échéant.
         Ceci doit être fait à la fois par l'interpréteur de commandes et dans les
         processus enfants individuels à cause des conditions de course potentielles..  */

		pid = getpid (); 					// on recupere son ID
      if (pgid == 0) pgid = pid; 
      setpgid (pid, pgid); 					// on le défini comme ID de groupe 
      if (foreground) 						// si on est au premier plan on doit le placer au 1er plan partout
        tcsetpgrp (shell_terminal, pgid); 	// dans le sous shell et le shell 

    /* Remettre la gestion des signaux de contrôle des travaux à leur valeur par défaut.
        Car on ne veut pas que les process enfant ignore les signaux  */
      signal (SIGINT, SIG_DFL);
      signal (SIGQUIT, SIG_DFL);
      signal (SIGTSTP, SIG_DFL); // exemple arrer d'un process , il doit prevenir tout le groupe
      signal (SIGTTIN, SIG_DFL);
      signal (SIGTTOU, SIG_DFL);
      signal (SIGCHLD, SIG_DFL);
 
    if (infile != STDIN_FILENO) {
		if(close(STDIN_FILENO) < 0) 
			printf("ERROR: Could not close STDIN\n");
		if(dup(infile) != STDIN_FILENO)
					printf("ERROR: Could not dup infile\n");
	}

	/* Gestion des entree et sortie */
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
		
  /* Exécutez le nouveau processus.  Assurez-vous que nous sortons.  */
  execvp (p->argv[0], p->argv);
  perror ("execvp");
  exit (1);
}


/* Store the status of the process pid that was returned by waitpid.
Return 0 if all went well, nonzero otherwise.  */	

void launch_job(job *j, int foreground) // Pour lancer un job 
{
  process *p; // on a un process
  pid_t pid;
  int mypipe[2], infile, outfile;
  infile = j->stdin;  // On recupere l'entrée 
  for (p = j->first_process; p; p = p->next) // On parcours tous les process
    {
      if (p->next) // si il y a un next 
        {
          if (pipe (mypipe) < 0)
            {
              perror ("pipe");
              exit (1);
            }
          outfile = mypipe[1];
        }
      else
        outfile = j->stdout; // flux de sortie 

      /* Fork the child processes.  */
      pid = fork ();
      if (pid == 0)
        /* This is the child process.  */
        launch_process (p, j->pgid, infile,
                        outfile, j->stderr, foreground); // on execute le process
      else if (pid < 0)
        {
          /* The fork failed.  */
          perror ("fork");
          exit (1);
        }
      else
        {   // si le pid > 0 alors c'est le parent
          /* This is the parent process.  */
          p->pid = pid;
          if (shell_is_interactive)
            {
              if (!j->pgid)
                j->pgid = pid;
              setpgid (pid, j->pgid); //on défini l'ID du process
            }
        }

      /* Clean up after pipes.  */
      if (infile != j->stdin)
        close (infile);
      if (outfile != j->stdout)
        close (outfile);
      infile = mypipe[0];
    }
  if (!shell_is_interactive)
    wait_for_job (j); // on attend le prochain job 
  else if (foreground)
    put_job_in_foreground (j, 0); // on le place au 1er plan 
  else
    put_job_in_background (j, 0); // on le place en arrière plan 
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

int cprep( const char *source, const char *cible){
  
    DIR *in = opendir(source);      // on ouvre le répertoire source
    DIR *out = opendir(cible);      // on ouvre le répertoire cible

    if(out==NULL){                  // Si la cible n'existe pas

        mkdir(cible,0777);          // On la crée

    }

    struct dirent *dp;      // déclaration de la structure représentant le prochain élément du répertoire
    dp=readdir(in);         // on lit cet élément

    struct stat path_stat;  // On déclare la structure stat qui contiendra le type de l'élément (repertoire ou fichier) dans son champ st_mode

    while(dp!=NULL){        // tant qu'il y a des éléments dans le répertoire

        if(strncmp(dp->d_name,".",1) == 0){     // Si le nom de l'élement commence par un point on passe au suivant

            dp=readdir(in);
            continue ;

        }

        char path1[50];         // le path de départ
        char path2[50];         // le path d'arrivée
        char nomfichier[50];      // le nom de l'élément (fichier/répertoire) à copier

        strcpy(path1,source);               // On copie le nom du répertoire de départ dans le path de depart
        strcpy(path2,cible);                // On copie le nom du répertoire d'arrivée dans le path d'arrivée
        strcpy(nomfichier,dp->d_name);        // On copie dans nomfichier le nom du fichier représententé par le champ d_type de la structure   

        strcat(path1,"/");                  // On concatène "/" au path1
        strcat(path1,nomfichier);             // On concatène également le nom de l'élément pour avoir le path1 complet

        strcat(path2,"/");                  // On concatène "/" au path2
        strcat(path2,nomfichier);             // On concatène également le nom de l'élément pour avoir le path2 complet

        
        stat(path1, &path_stat);            // On initialise la structure sur le path 1
        
        //if(dp->d_type==DT_DIR)<-- On aurait pu faire ceci pour savoir si l'élément est un répertoire

        if(S_ISDIR(path_stat.st_mode)){       // Si l'élément est un répertoire

            mkdir(path2,0777);              // On crée sa "copie" vide
            cprep(path1,path2);             // on y ajoute tout le contenu de répertoire source (appel récursif)
            
        }
        else{
            cpfile(path1,path2);                // Sinon on copie le fichier du répertoire source vers le répertoire cible
        }

        dp=readdir(in);                     // On passe à l'élément suivant
        
    }
    closedir(in);                           // On ferme le répertoire source
    closedir(out);                          // On ferme le répertoire cible

    return 0;
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
		strncat(path, "/", 1);
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
		if (argv[0] == '\0')
			continue;

		// On regarde quelle commande a été entrée : 

		else if (strcmp(argv[0], "exit") == 0)  //   on sort du programme      
			return 0;                                  
		else if (strcmp(argv[0], "cd") == 0) // on execute la fonction cd
			cd (argv[1]);
		else if (strcmp(argv[0], "cp") == 0) // on execute la fonction cp
			cp(argv[1], argv[2]);
		else if (strcmp(argv[0], "help") == 0) // on execute la fonction cp
			help();	

		// Sinon c'est que l'on execute un programme             	
		else {
			if ((first_job = job_initialize(argv, tokens, pforeground)) != NULL) {
				launch_job(first_job, foreground);
				free_job(first_job);
			}
		}
	}
}
