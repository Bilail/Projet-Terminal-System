#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>


int copyfile(const char *iname , const char *oname){

    int idesc = open(iname, O_RDONLY); // on ouvre en lecture seulement 
    /*struct stat st;      
    fstat (idesc, &st);  sans chmod */
    int odesc = open(oname, O_WRONLY | O_CREAT | O_EXCL, 0666);

    struct stat istat;
    fstat(idesc, &istat);
    fchmod(odesc, istat.st_mode); // passer par chmod
    
    while(1){
        char buffer[4096];
        int rcnt = read(idesc, buffer, sizeof(buffer));
        if (rcnt == 0)
            break;
        int pos = 0;
        while (rcnt != 0){
            int wcnt = write (odesc, buffer + pos, rcnt); 
            rcnt -= wcnt; // On enleve ce que l'on a écrit
            pos += wcnt; // on reprend l'ecriture la ou on c'est arrété 
        }
    }
    close(idesc);
    close(odesc);
    return 0;

}

int main() {

copyfile("cp.c", "testcp.c");

}