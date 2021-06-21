



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
init_shell ()
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