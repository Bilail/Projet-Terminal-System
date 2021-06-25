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


/*      Nos Structure de données      */
/* Processus   */
typedef struct process
{
  struct process *next;       /* pointeur vers le prochain processus  */
  char **argv;                /* poru executer  */
  pid_t pid;                  /* process ID */
  char completed;             /* true si le process est completed */
  char stopped;               /* true si le process est  stopped */
  int status;                 /* reported status value */
} process;

/* Job - Liste de processus   */
typedef struct job
{
  struct job *next;           /* pointeur vers le prochain job */
  char *command;              /* command line, used for messages */
  process *first_process;     /* pointeur vers les processus du job - nottament le 1er */
  pid_t pgid;                 /* process group ID */
  char notified;              /* true si l'utilisateur a stopped job */
  struct termios tmodes;      /* saved terminal modes */
  int stdin, stdout, stderr;  /* standard i/o channels */
} job;

/* The active jobs are linked into a list.  This is its head.  
On initialise le job - pour le moment il ne pointe rien */
job *first_job = NULL;

/* -------- Fonction Utile --------*/

/* Trouver le job active avec comme paramétre le pgid */
job * find_job (pid_t pgid)
{
  job *j;
    // On parcours tous les job
  for (j = first_job; j; j = j->next)
    if (j->pgid == pgid) // On regarde si le pgid est le même 
      return j;         // Si c'est le cas on le return 
  return NULL;         // Si on ne trouve pas de correspondance on renvoie NULL 
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

/* --------- Initialisation du shell ------------------*/

/* Keep track of attributes of the shell.  */

#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;


/* On doit s'assurer que le shell est interractive 
        et en arrière plan */

void
init_shell()
{

  /* On regarde si il est interractive   */
  shell_terminal = STDIN_FILENO;
  shell_is_interactive = isatty (shell_terminal);

  if (shell_is_interactive)
    {
      /* Boucle jusqu'à ce que notre shell soit en arrière plan  */
      /* tcgetpgrp : permet de recuperer un ID que l'on compare avec celui du premier plan actuel */
      while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
        kill (- shell_pgid, SIGTTIN); // arrete le process

      /* Ignorer les signaux interactifs et de contrôle du travail
            Pour ne pas s'arrerter par accident  */
      signal (SIGINT, SIG_IGN);
      signal (SIGQUIT, SIG_IGN);
      signal (SIGTSTP, SIG_IGN);
      signal (SIGTTIN, SIG_IGN);
      signal (SIGTTOU, SIG_IGN);
      signal (SIGCHLD, SIG_IGN);

      /* Se placer dans son propre groupe de processus */
      shell_pgid = getpid ();
      // setpgid : Définir l'ID du groupe de processus pour le contrôle des travaux
      if (setpgid (shell_pgid, shell_pgid) < 0)
        {
          perror ("Couldn't put the shell in its own process group");
          exit (1);
        }

      /* On prend le contrôle du terminal. */
      tcsetpgrp (shell_terminal, shell_pgid);

      /* Sauvegarder les attributs de terminal par défaut pour le shell.  */
      tcgetattr (shell_terminal, &shell_tmodes);
    }
}


/* ------ Lancement de travaux --------*/

void launch_process (process *p, pid_t pgid,
   int infile, 
   int outfile,
   int errfile,
   int foreground)
{
  pid_t pid;

  if (shell_is_interactive)
    {
      /* Placez le processus dans le groupe de processus et donnez au groupe de processus
         le terminal, le cas échéant.
         Ceci doit être fait à la fois par l'interpréteur de commandes et dans les
         processus enfants individuels à cause des conditions de course potentielles..  */

      pid = getpid (); // on recupere son ID
      if (pgid == 0) pgid = pid; 
      setpgid (pid, pgid); // on le défini comme ID de groupe 
      if (foreground) // si on est au premier plan on doit le placer au 1er plan partout
        tcsetpgrp (shell_terminal, pgid); // dans le sous shell et le shell 

    /* Remettre la gestion des signaux de contrôle des travaux à leur valeur par défaut.
        Car on ne veut pas que les process enfant ignore les signaux  */
      signal (SIGINT, SIG_DFL);
      signal (SIGQUIT, SIG_DFL);
      signal (SIGTSTP, SIG_DFL); // exemple arrer d'un process , il doit prevenir tout le groupe
      signal (SIGTTIN, SIG_DFL);
      signal (SIGTTOU, SIG_DFL);
      signal (SIGCHLD, SIG_DFL);
    }

  /* Définir les canaux d'entrée/sortie standard du nouveau processus.  */
  if (infile != STDIN_FILENO)
    {
      dup2 (infile, STDIN_FILENO);
      close (infile);
    }
  if (outfile != STDOUT_FILENO)
    {
      dup2 (outfile, STDOUT_FILENO);
      close (outfile);
    }
  if (errfile != STDERR_FILENO)
    {
      dup2 (errfile, STDERR_FILENO);
      close (errfile);
    }

  /* Exécutez le nouveau processus.  Assurez-vous que nous sortons.  */
  execvp (p->argv[0], p->argv);
  perror ("execvp");
  exit (1);
}

void launch_job (job *j, int foreground) // Pour lancer un job 
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
        {   // si le pid > 0 alors c le parent
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

  format_job_info (j, "launched");

  if (!shell_is_interactive)
    wait_for_job (j); // on attent le prochain job 
  else if (foreground)
    put_job_in_foreground (j, 0); // on le place au 1er plan 
  else
    put_job_in_background (j, 0); // on le place en arrière plan 
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


  /*  Si le groupe de processus est lancé en tant que tâche en arrière-plan, 
  le shell doit rester au premier plan lui-même et continuer à lire les commandes à partir du terminal.
   ----------------------------------------------
   On met un job en background, Si cont est truen on lui envoie un signal 
   pour le reveiller  */

void put_job_in_background (job *j, int cont)
{
  /* Send the job a continue signal, if necessary.  */
  if (cont)
    if (kill (-j->pgid, SIGCONT) < 0)
      perror ("kill (SIGCONT)");
}


/* -------- Nos fonctions CD et CP ----------*/

// La fonction cd pour atteindre un dossier
void cd (char * dir) {
	char path[100];
	
	if (dir != NULL) {
		getcwd(path, sizeof(path));     //  ON recupere le repertoire actuelle
		strncat(path, "/", 1);
		strncat(path, dir, strlen(dir));		//  On ajoute le nouveau chemin
		if (chdir(path) < 0)		//  ON vérifie les erreurs
			printf("ERROR: chemin non trouvé %s\n", dir);			
		return;		
	}

 
//Fonction pour copier un fichier
void cpfile(const char *src , const char *dest){

    int fsrc = open(src, O_RDONLY); // on ouvre en lecture seulement 
    /*struct stat st;      
    fstat (fsrc =, &st);  sans chmod */
    int fdest = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0666);

    struct stat istat;
    fstat(fsrc, &istat);
    fchmod(fdest, istat.st_mode); // passer par chmod
    
    while(1){
        char buffer[4096];
        int rcnt = read(fsrc, buffer, sizeof(buffer));
        if (rcnt == 0)
            break;
        int pos = 0;
        while (rcnt != 0){
            int wcnt = write (fdest, buffer + pos, rcnt); 
            rcnt -= wcnt; // On enleve ce que l'on a écrit
            pos += wcnt; // on reprend l'ecriture la ou on c'est arrété 
        }
    }
    close(fsrc);
    close(fdest);

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
                strcpy(path_src,src); // On copie dans path_src le chemin de src 
            	strcpy(path_dest,dest); // On copie dans path_dest le chemin de dest 
           		strcpy(filename, pd->d_name); //on recupere le nom du fichier sur lequel pointe pd

           		strcat(path_src,"/"); // on rajoute / au chemin path_src
           		strcat(path_src,filename); // on lui ajoute ensuite le nom du fichier pointe

            	strcat(path_dest,"/");   // On fais la même chose pour path_dest 
            	strcat(path_dest,filename);

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

}

int  main(int argc, char ** argvFILE) {

  init_shell();
  while(1){

    
  }

}