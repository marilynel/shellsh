#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> // pid_t
#include <sys/wait.h> // for waitpid
#include <unistd.h> // fork


int foregroundMode = 0;         // if 0 -> not in foreground mode; if 1 -> foreground only mode
int sigtstpNotice = 0;          // if latch = 0 -> don't print foreground message; if 1 -> print foreground message

// handler function for SIGINT: ctrl+c
void handle_SIGINT(int signo) {
    // do nothing when ctrl+c is hit, ignore signal
}

// handler for SIGUSR2 for handling "exit" command
void handle_SIGUSR2(int signo) {
    exit(0);
}

// handler function for SIGSTP: ctrl+z
void handle_SIGTSTP(int signo) {
    if (foregroundMode == 1) {
        foregroundMode = 0;
    } else {
        foregroundMode = 1;
    }
    sigtstpNotice = 1;
}


int main(){
    struct sigaction SIGINT_action = {0}, SIGUSR2_action = {0}, SIGTSTP_action = {0};

    // fill in SIGINT_actino struct for blocking SIGINT
    SIGINT_action.sa_handler = handle_SIGINT;
    sigfillset(&SIGINT_action.sa_mask);     // block all catchable signals while handler is running
    SIGINT_action.sa_flags = 0;             // no flags set
    sigaction(SIGINT, &SIGINT_action, NULL);

    // fill in SIGUSR2_action struct for exiting
    SIGUSR2_action.sa_handler = handle_SIGUSR2;
    sigfillset(&SIGUSR2_action.sa_mask);
    SIGUSR2_action.sa_flags = 0;
    sigaction(SIGUSR2, &SIGUSR2_action, NULL);

    // fill in SIGTSTP_action struct for blocking SIGTSTP
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // initialize status
    int status = 0;   
    while (1) {
        // check for SIGTSTP interruption
        if (sigtstpNotice == 1) {
            if (foregroundMode == 0) {
                printf("\nExiting foreground-only mode\n");
                fflush(stdout);
            } else if (foregroundMode == 1) {
                printf("\nEntering foreground-only mode (& is now ignored)\n");
                fflush(stdout);
            }
            sigtstpNotice = 0;
        }

        char command_array[2048];
        char *command = &command_array[0];
        memset(command_array,0,2048);           // set values in command array to 0
        printf(":");
        fflush(stdout);
        fgets(command, 2048, stdin);

        // 'fgets' keeps the newline at the end; strip '\n' from the end of the command
        command[strcspn(command, "\n")] = 0;

        // check for whitespace in front of command and skip it
        while(isspace(*command)) {
            command++;
        }

        // check for comments
        if (strncmp(command, "#", 1) == 0) {
            continue;
        }

        // check for user just hitting 'enter'
        if (command[0] == '\0') {
            continue;
        }

        // exit program command
        if (strcmp(command,"exit") == 0) {
            raise(SIGUSR2);
        }
        
        // status command
        if (strcmp(command,"status") == 0) {       
            printf("exit value %d\n",status);
            fflush(stdout);
            continue;
        }

        // cd command
        if (strncmp(command,"cd ", 3) == 0) {
            status = chdir(command+3);              // ptr arith: command->"cd .." thus command+3->".."
            continue;
        }

        // tokenize command array to turn into array for execvp()
        char *args[512];
        char* token;
        char* firstCommand;
        token = strtok(command, " ");
        args[0] = calloc(strlen(token)+1, sizeof(char));
        strcpy(args[0], token);
        firstCommand = calloc(strlen(token)+1, sizeof(char));
        strcpy(firstCommand, token);
        int i = 1;
        while (token != NULL) {
            token = strtok(NULL, " ");
            if (token != NULL) {
                args[i] = calloc(strlen(token)+1, sizeof(char));
                strcpy(args[i], token);
                i++;
            }            
        }
        args[i] = NULL;     // set last item in args to null

        // look for <, >, $$, and & in command and set corresponding flags
        int writeTo, writeFrom, ampersand = 0;
        int writeToIndex, writeFromIndex = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(args[j],">") == 0) {
                writeTo = 1;
                writeToIndex = j;
            }
            if (strcmp(args[j],"<") == 0) {
                writeFrom = 1;
                writeFromIndex = j;
            }

            // replace '$$' with pid
            if (strcmp(args[j],"$$") == 0) {
                pid_t pid = getpid();
                char pidchar[80];
                
                sprintf(pidchar, "%d", pid);
                args[j] = calloc(strlen(pidchar)+1, sizeof(char));
                strcpy(args[j], pidchar);
            }
        }

        if (strcmp(args[i-1],"&")==0) {
            ampersand = 1;
        }

        // spawn a child to run exec function
        int child;
        pid_t spawnpid = fork();
        switch (spawnpid) {
        case -1:
            perror("forked up\n");
            exit(1);
            break;
        case 0: ;    
            // open new files, if necessary 
            int out, savestdout = 0;
            int infd = -1;
            if (writeTo == 1) {
                out = open(args[writeToIndex+1], O_WRONLY | O_CREAT | O_TRUNC, 0640);
                if (out == -1) {
                    perror("cannot open file for input");
                    exit(1);
                }
            }
            if (writeFrom == 1) {
                infd = open(args[writeFromIndex+1], O_RDONLY, 0640);
                if (infd == -1) {
                    perror("cannot open file for input");
                    exit(1);
                }
            }
            if (writeTo == 1) {
                int result = dup2(out, 1);
                if (result == -1) {
                    perror("error in dup2()\n");
                    exit(1);
                }
                args[writeToIndex] = NULL;
            }
            if (writeFrom == 1) {
                int result = dup2(infd, 0);
                if (result == -1) {
                    perror("error in dup2()\n");
                    exit(1);
                }
                args[writeFromIndex] = NULL;
            }

            if (ampersand == 1) {
                // run this command in the background
            }
            
            // exec function to run command
            execvp(args[0], args);
            // following only invoked if exec fails:
            exit(2);

        default:
            // in the parent process
            spawnpid = waitpid(spawnpid, &child, 0);
            // check if child returned normally
            status = -1;      
            if (WIFEXITED(child)) {
                status = WEXITSTATUS(child);
            }
            fflush(stdout);
            // reset variables
            writeTo = 0;
            writeFrom = 0;
            ampersand = 0;
        }
    }
    return 0;
}



