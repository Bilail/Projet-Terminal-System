#define _POSIX_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
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

# define WAIT_ANY -1
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
 sans bloquer si le job n'est pas arretés ou terminé  */

void update_status (void)
{
  int status;
  pid_t pid;

  do
    pid = waitpid (WAIT_ANY, &status, WUNTRACED|WNOHANG);     // suspendre l'éxecution
  while (!mark_process_status (pid, status));                 // tant qu'il y a des processus dont le statut n'est pas mis à jour
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

/* Affichage des information concernant le job pour l'utilisateur  */

void format_job_info (job *j, const char *status)
{
  fprintf (stderr, "%ld (%s): %s\n", (long)j->pgid, status, j->command);
}

/* Fonction notifiant l'utilisateur des jobs arretés ou terminés,
   Supprime les jobs terminés de la liste des jobs actifs  */

void do_job_notification (void)
{
  job *j, *jlast, *jnext;

  /* màj du status des processus enfants  */
  update_status ();

  jlast = NULL;
  for (j = first_job; j; j = jnext)
    {
      jnext = j->next;

      
      if (job_is_completed (j)) {               // Si le job est terminé
        format_job_info (j, "completed");       // On informe l'utilisateur
        if (jlast)                              // Si le dernier job n'était pas nul (il s'est arreté)
          jlast->next = jnext;                  // Le job suivant du dernier traité job devient le job suivant du job actuel
        else                                    // Sinon le job actuel devient le premier job (on entre ici qu'une seule fois) jlast étant initialisé à NULL avant la boucle
          first_job = jnext;
        free (j);                           // On supprime le job de la liste des jobs actifs
      }

      
      else if (job_is_stopped (j) && !j->notified) {      // Si le job est arreté et non marqué
        format_job_info (j, "stopped");                   // On informe l'utilisateur
        j->notified = 1;                                  // On marque le job pour ne pas repasser dessus
        jlast = j;                                        // Le dernier job traité devient le job actuel
      }

      
      else                      //Job toujours en cours
        jlast = j;         
    }
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

  format_job_info (j, "launched");

  if (!shell_is_interactive)
    wait_for_job (j); // on attend le prochain job 
  else if (foreground)
    put_job_in_foreground (j, 0); // on le place au 1er plan 
  else
    put_job_in_background (j, 0); // on le place en arrière plan 
}



void  parse(char *line, char **argv, int  *tokens) {
	*tokens = 0;
	while (*line != '\0') {       /* if not the end of line ....... */ 

		while (isspace(*line) || iscntrl(*line)) {
			if (*line == '\0') {
				*argv = '\0';			
				return;
			}
			*line++ = '\0';     /* replace white spaces with NULL terminator   */
		}
	         /* save the argument pos     */ 
		if(*line != '<' && *line != '>' && *line != '|') {
			*argv++ = line; 
			(*tokens)++;
		}
		while (isalnum(*line) || ispunct(*line)) {
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
				line++;             /* skip the argument until ...    */
		}
	}
	*argv = '\0';                 /* mark the end of argument list  */
}


// La fonction cd pour atteindre un dossier
void cd (char * dir) {
	char path[100];
	
	if (dir != NULL) {
		getcwd(path, sizeof(path));     //  ON recupere le repertoire actuelle
    strncat(path, "/", 1);
    size_t length=strlen(path);
		strncat(path, dir, length-1);		//  On ajoute le nouveau chemin
		if (chdir(path) < 0)		//  ON vérifie les erreurs
			printf("ERROR: chemin non trouvé %s\n", dir);			
		return;		
	}
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
}

char *read_line() {
  int bufsize = 1024;
  int pos = 0;
  char *buffer = malloc(sizeof(char) * bufsize);
  int c;

  if (!buffer) {
    fprintf(stderr, "erreur: allocation error\n");
    exit(EXIT_FAILURE);
  }
  while (1) {

    // ON lit un caractère 
    c = getchar();

    if (c == EOF) {
      exit(EXIT_SUCCESS);
    } else if (c == '\n') { // on est arrivé à la fin 
      buffer[pos] = '\0';
      return buffer;
    } else { // on recupere le caractère 
      buffer[pos] = c;
    }
    pos++;

    // Si le buffer n'est pas assez grand 
    if (pos >= bufsize) {
      bufsize += 1024;
      buffer = realloc(buffer, bufsize);
      if (!buffer) {
        fprintf(stderr, "erreur: allocation error\n");
        exit(EXIT_FAILURE);
      }
    }
  }
}


/* -------- HELP -------- */
void help(char **args){
  printf(" \t\tSHELL Polytech Paris Saclay \n");
  printf("\t\t-----------------------------\n");
  printf("\t\tréalisé par Natanael et Bilail\n");
}

int  main(int argc, char ** argv) {

  /* Liste des commandes 
  - cd
  - ls
  - cp
  - exit 
  - help 
  */

  init_shell();
  printf(" \t\tSHELL Polytech Paris Saclay \n");
  printf("\t\t-----------------------------\n");
  printf("\t\tréalisé par Natanael et Bilail\n");


  char *line;
  char **args;
  int status;


  while(1)
  {
    printf("Polytech Paris Saclay > ");
    line = read_line();
    printf(line);

    if(strcmp(argv[0],"help")==0){
      help(argv);
    }
      /* -- -Execution de la fonction CP --*/
    if (strcmp(argv[0],"cp")==0){
      if (argc == 3){
        // On recupere les entrée systemes
        cp(argv[1], argv[2]);
      }
      else {
        // il faut un src et un dest
        printf(" Il faut entrer deux arguments ! \n");
      }
    }

    /* -- -Execution de la fonction CD --*/
    if (strcmp(argv[0],"cd")==0){
        cd(argv[1]);
    }


  }
  return 0;
}