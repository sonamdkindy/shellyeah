/*******************************************************************************
 * Sonam Kindy
 * Description: shellyeah (implementation file)
 * Created:     11/4/18
 ******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fcntl.h>
#include "shellyeah.h"

/* global var to store bg processes; used for exit */
processes* bgProcesses; 
/* global flag for foreground only mode (only fg processes can be run) */
int fgOnlyMode = 0;
/* global flag for SIGTSTP sig caught before fg process done */
int SIGTSTPcaughtBeforeWait = 0;

/*******************************************************************************
 * int main() 
 * 
 * Description: Main driver initializes global struct to keep track of background 
 * processes and prompt user for input until exit at which point memory is de-
 * allocated for the struct.
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
int main()
{
    bgProcesses = initProcessesStruct();
    prompt();
    free(bgProcesses);
    return 0;
}

/*******************************************************************************
 * void prompt()
 * 
 * Description: This function handles gathering user input and $$ expansion then 
 * dispatches the input to be executed as a command. It also sets up the sig 
 * actions for SIGINT and SIGTSTP for the parent. Prompt continues until user 
 * input is "exit".
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void prompt()
{
    char userInput[MAX_CL_CHARS]; /* max number of chars in command line is 2048 */
    /* check for "exit" as input */
    char first4Chars[5];
    memset(first4Chars, '\0', 5);

    /* to contain the status of the last foreground process */
    char* status = (char*)malloc(30*sizeof(char));
    memset(status, '\0', 30);
    sprintf(status, "exit value 0");

    /* for use in $$ str expansion to the shell pid */
    char* substr = "$$";
    char pidStr[20];
    memset(pidStr, '\0', sizeof(pidStr));
    sprintf(pidStr, "%d", getpid());

    ignoreAction(SIGINT); /* shell/parent should ignore SIGINT */
    registerParentSIGTSTPaction(); /* shell/parent should have SIGTSTP toggle fg only mode */

    /* keep prompting user for input until exit command input */
    do {
        reapZombies();
        memset(userInput, '\0', sizeof(userInput));
        printf(": "); /* print prompt */
        fflush(stdout); 
        fgets(userInput, MAX_CL_CHARS, stdin); /* get input from user */
        userInput[strcspn(userInput, "\n")] = '\0'; /* strip newline */
        /* replace $$ with pid of shell*/
        replaceStr(userInput, substr, pidStr);
        /* command inputted; parse and execute command */
        if(strcspn(userInput, "#") == strlen(userInput) && strlen(userInput) != 0)
            dispatch(userInput, &status);
        memcpy(first4Chars, userInput, 4); /* copy first 4 chars of command line input to 
                                              check for 'exit' */
    } while(strcmp(first4Chars, "exit") != 0);

    free(status);
}

/*******************************************************************************
 * void dispatch(char* userInput, char** status)
 * 
 * Description: This function takes as input the user input and pointer to a str 
 * status. Based on the input, it assigns the input/output redirection filenames
 * and carries out the appropriate command.
 * params:      userInput (char*)
 *              status (char**)
 * ret:         N/A
 ******************************************************************************/
void dispatch(char* userInput, char** status)
{
    int bgProcessFlag = 0; /* default is not a bg process; foreground process instead */

    char redirectInput[MAX_CL_CHARS];
    char redirectOutput[MAX_CL_CHARS];
    memset(redirectInput, '\0', sizeof(redirectInput));
    memset(redirectOutput, '\0', sizeof(redirectOutput));

    char** args = getArgs(userInput, &bgProcessFlag); /* populate argArr with each arg in args str */

    /* check for built-in commands: exit, status, cd */
    if(strcmp(args[0], "exit") == 0)
        /* kill all bg processes then exit prompt and prog */
        killAll(bgProcesses);
    else if(strcmp(args[0], "status") == 0) {
        printf("%s\n", *status);
        fflush(NULL);
    }
    else if(strcmp(args[0], "cd") == 0) {
        char path[MAX_CHARS_PATH]; /* to store path */
        memset(path, '\0', sizeof(path)); /* make sure path str null terminates */
        /* if there was a dir specified (second arg) copy to path */
        if(args[1] != NULL)
            strcpy(path, args[1]);
        changedir(path); /* path will be an empty str or path str */ 
    }
    /* not built-in; exec call with command and redirect stdin & stdout streams to files */
    else {
        /* if the strings "<" or ">" exist in the user input */
        if(strstr(userInput, "<") != NULL || strstr(userInput, ">") != NULL) {
            const char inputRedirChar[2] = "<";
            const char outputRedirChar[2] = ">";
            const char delim[2] = " ";
            char* token;

            int i;
            /* figure out where we need to redirect input and output;
             * NOTE: I'm doing more than necessary based on changes I made so this could be
             * reworked */
            for(i = 0; i < MAX_ARGS && args[i] != NULL && args[i + 1] != NULL; i++) {
                /* if the current arg is "<" */
                if(strcmp(args[i], inputRedirChar) == 0) {
                    token = strtok(args[i + 1], delim); /* next arg is input redir file */
                    token = strtok(token, outputRedirChar); /* split on > so we get the input 
                                                               redir file */
                    strcpy(redirectInput, token); /* copy the token str to redirectInput */
                } else if(strcmp(args[i], outputRedirChar) == 0) {
                    token = strtok(args[i + 1], delim); /* next arg is output redir file */
                    strcpy(redirectOutput, token);
                }
                if(strcmp(args[i], inputRedirChar) == 0 || 
                   strcmp(args[i], outputRedirChar) == 0)
                    i++; /* don't need to look at next arg; already processed above */
            }
        }
        /* fork child process, handle i/o redirection as necessary, and call exec func */
        execCommand(args[0], args, redirectInput, redirectOutput, &bgProcessFlag, status);
    }
    
    freeArgs(args); /* free memory allocated to arr of strs args */
}

/*******************************************************************************
 * void reapZombies()
 * 
 * Description: This function reaps the processes that have finished and are ready to
 * be reaped by the parent process. For each process that is reaped, it outputs its
 * pid and how it exited.
 * params:      userInput (char*)
 *              status (char**)
 * ret:         N/A
 ******************************************************************************/
void reapZombies()
{
    int spawnpid = -5;
    int childExitStatus = -5;

    /* while there is a zombie process, output that it's done */
    while((spawnpid = waitpid(-1, &childExitStatus, WNOHANG)) > 0) {
        printf("background process %d is done: ", spawnpid);
        printExitStatus(childExitStatus);
    }
}

/*******************************************************************************
 * void replaceStr(char* str, char* substr, char* replacement)
 * 
 * Description: Replace all occurrences of substr in str with replacement str.
 * Source: https://stackoverflow.com/questions/32413667/replace-all-occurrences-of-a-substring-in-a-string-in-c
 * params:      str (char*)
 *              substr (char*)
 *              replacement (char*)
 * ret:         N/A
 ******************************************************************************/
void replaceStr(char* str, char* substr, char* replacement) 
{
    /* set a character buffer the same size as the input str */
    char buffer[MAX_CL_CHARS];
    /* make sure the buffer null terminates */
    memset(buffer, '\0', sizeof(buffer));
    char* temp = str; /* assign a temp ptr to str */
    /* while temp contains substr */
    while( (temp = strstr(temp, substr)) ){
        /* copy temp-str bytes of the str to buffer */
        strncpy(buffer, str, temp - str);
        /* cat buffer with the replacement str */
        strcat(buffer, replacement);
        /* then cat the rest of the str after the substr (exclude only the substr) */
        strcat(buffer, temp + strlen(substr));
        /* now copy what's in the buffer to str */
        strcpy(str, buffer);
        /* increment the temp ptr so we're looking at the next part of the str */
        temp++;
    }
}

/*******************************************************************************
 * void replaceStr(char* str, char* substr, char* replacement)
 * 
 * Description: This function takes as input the user input and pointer to an int
 * that represents whether the command should be executed in the background. The 
 * user input is parsed and each space-delimited value is added to an array of 
 * strings called args, which is returned.
 * Source: https://stackoverflow.com/questions/32413667/replace-all-occurrences-of-a-substring-in-a-string-in-c
 * params:      userInput (char*)
 *              bgProcess (int*)
 * ret:         args (char**)
 ******************************************************************************/
char** getArgs(char* userInput, int* bgProcess)
{
    /* make copy of command line str so ptr manip/changes do not persist */
    char userInputCpy[strlen(userInput)];
    memset(userInputCpy, '\0', sizeof(userInputCpy));
    strcpy(userInputCpy, userInput);
   
    /* arr of strs to store each arg in args str (room for 512 strs) */
    char** args = malloc(MAX_ARGS * sizeof(char*));

    int i;
    for(i = 0; i < MAX_ARGS; i++)
        args[i] = NULL;

    const char delim[2] = " ";
    char* token;
    token = strtok(userInputCpy, delim); /* get first token */
    i = 0;
    while(token != NULL) {
        args[i] = (char*)malloc(MAX_CL_CHARS*sizeof(char)); /* designate default size for 
                                                               each arg str in args */
        memset(args[i], '\0', MAX_CL_CHARS); /* make sure the current arg null terminates */
        
        strcpy(args[i], token); /* copy token str to args at i */

        token = strtok(NULL, delim); /* get next token */
        
        i++; /* next available index in args str arr */
    }

    /* update whether this process as indicated by args should run in the bg */
    *bgProcess = strcmp(args[i - 1], "&") == 0 && !fgOnlyMode;
    /* if last arg is & remove it */
    if(strcmp(args[i - 1], "&") == 0) {
        free(args[i - 1]);
        args[i - 1] = NULL;
    }

    return args; 
}

/*******************************************************************************
 * void changedir(char* path)
 * 
 * Description: This function takes as input a dir path. If its length is zero, i.e.
 * an empty str, the func cd's into the home dir as indicated by the HOME env var;
 * otherwise, it cd's into the specified dir path.
 * params:      path (char*)
 * ret:         N/A
 ******************************************************************************/
void changedir(char* path)
{
    char home[100];
    memset(home, '\0', sizeof(home));
    strcpy(home, getenv("HOME")); /* copy env var HOME to home path */

    int res = -1;
    /* if no path indicated, cd to home dir */
    if(strlen(path) == 0)
        res = chdir(home);
    /* else path indicated, cd to path */
    else
        res = chdir(path);

    /* in the event of an error, output the error msg */
    if(res == -1) {
        printf("%s: no such directory\n", path);
        fflush(stdout);
    }
}

/*******************************************************************************
 *void execCommand(char* command, char** args, char* infile, char* outfile, 
 *                 int* isBgProcess, char** status)
 * 
 * Description: This function forks a child process to execute the given command with
 * the given str arr. It redirects the i/o as specified by the infile and outfile
 * as necessary based on whether they're non-empty strings or if the command should
 * be run in the background (/dev/null). The status is also updated via the param 
 * of a pointer to a str. 
 * params:      command (char*)
 *              args (char**)
 *              infile (char*)
 *              outfile (char*)
 *              isBgProcess (int*)
 *              status (char**)
 * ret:         N/A
 ******************************************************************************/
void execCommand(char* command, char** args, char* infile, char* outfile, 
                 int* isBgProcess, char** status)
{
    /* init to known erroneous values */
    int sourceFD = -1,
        targetFD = -1;
    int childExitStatus = -5;
    pid_t spawnpid = -5;

    spawnpid = fork();

    /* catch fork() failure */
    if(spawnpid == -1) {
        perror("fork() failed: hull breach");
        exit(1);
    } 
    /* child process */
    else if (spawnpid == 0) {
        /* child process should terminate if SIGTERM */
        registerChildSIGTERMaction();

        /* child process should ignore SIGTSTP */
        ignoreAction(SIGTSTP);

        /* child process should exit if SIGINT */
        if(*isBgProcess == 0)
            registerChildSIGINTaction();

        int resDupInput = 0,
            resDupOutput = 0;

        /* handle redirection */
        /* if infile specified */
        if(strlen(infile)) {
            sourceFD = open(infile, O_RDONLY); /* open spec. file for reading */
            /* catch error for open() */
            if(sourceFD == -1) {
                printf("cannot open %s for input\n", infile);
                fflush(stdout);
                exit(1);
            }

            /* as long as opening file was successful, redirect stdin to spec. file */
            if(sourceFD != -1) 
                resDupInput = dup2(sourceFD, STDIN_FILENO);
            /* catch error for dup2() */
            if(resDupInput == -1) {
                perror("dup2() failed on sourceFD");
                exit(1);
            }
        }
        /* if outfile specified */
        if(strlen(outfile)) {
            /* open spec. file for writing; create if not exists and overwrite */
            targetFD = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(targetFD == -1) {
                printf("cannot open %s for output\n", outfile);
                fflush(stdout);
                exit(1);
            }
            
            /* as long as opening file was successful, redirect stdin to spec. file */
            if(targetFD != -1)
                resDupOutput = dup2(targetFD, STDOUT_FILENO);
            /* catch error for dup2() */
            if(resDupOutput == -1) {
                perror("dup2() failed on targetFD");
                exit(1);
            }
        }

        /* background commands should have their standard input and output redirected 
         * from /dev/null if the user did not specify another file or other files */
        if(*isBgProcess == 1) {
            sourceFD = targetFD = -1;
            char* defaultFile = "/dev/null";
            /* if output redir file not specified, open /dev/null for reading */
            if(!strlen(outfile))
                targetFD = open(defaultFile, O_RDONLY);
            /* if input redir file not specified, open /dev/null for writing */
            if(!strlen(infile))
                sourceFD = open(defaultFile, O_WRONLY | O_TRUNC);
        }
       
        /* if input or output redir files are specified, use execlp; else execvp */
        if(strlen(infile) || strlen(outfile)) {
            execlp(args[0], args[0], NULL);
            /* only reason child process would get to this line is if exec failed */
            printf("%s: command not found\n", args[0]);
            fflush(stdout);
        } else {
            execvp(args[0], args);
            /* only reason child process would get to this line is if exec failed */
            printf("%s: no such file or directory\n", args[0]);
            fflush(stdout);
        }
        exit(1); /* exit with non-zero status code indicating error */
    } 
    /* parent process */
    else {
        /* default block the parent process until child done executing */
        /* if bg process flag set, don't */
        pid_t actualpid;
        if(*isBgProcess == 1) {
            /* listen for terminated children */
            registerParentSIGCHLDaction();

            /* output pid of bg process just spawned */
            printf("background pid is %d\n", spawnpid);
            fflush(stdout);

            /* don't wait for it to finish; will be reaped it later */
            actualpid = waitpid(-1, &childExitStatus, WNOHANG);

            /* add the pid to struct of bg processes */
            bgProcesses->ids[bgProcesses->count++] = spawnpid;
        } else {
            /* toggle SIGTSTPcaughtBeforeWait global flag if SIGTSTP caught before 
             * fg process done */
            registerParentSIGTSTPactionBeforeWait();
           
            /* since fg (not bg) process restore default action for SIGCHLD signal */
            defaultAction(SIGCHLD);
            
            actualpid = waitpid(spawnpid, &childExitStatus, 0);

            /* update status for fg processes */
            updateExitStatus(childExitStatus, status);

            /* if the SIGTSTP sig was caught before the fg proess was done running 
             * (before waitpid), write the mode */
            if(SIGTSTPcaughtBeforeWait)
                writeMode();
            
            /* reset flag for whether SIGTSTP caught before waitpid to zero */
            SIGTSTPcaughtBeforeWait = 0; 

            registerParentSIGTSTPaction(); /* reset SIGTSTP handler to catchSignal */
        }
        /* catch waitpid() failure */
        if(actualpid == -1)
            perror("waitpid() failed");
    }
    /* close files as necessary */
    if(targetFD != -1)
        close(targetFD);
    if(sourceFD != -1)
        close(sourceFD);
}

/*******************************************************************************
 * void catchSignal(int signo)
 * 
 * Description: This function is the signal handler for all signals caught except
 * when SIGTSTP is caught before the child foreground process is done executing.
 * params:      signo (int)
 * ret:         N/A
 ******************************************************************************/
void catchSignal(int signo)
{
    char msg[MAX_CHARS_PATH];
    memset(msg, '\0', sizeof(msg));
    
    switch(signo) {
        /* CTRL-Z; parent toggles foreground only mode and writes mode to stdout */
        case SIGTSTP: {
            fgOnlyMode = !fgOnlyMode;
            writeMode();
            break;
        }
        /* child process done; parent reaps it */
        case SIGCHLD: {
            int childExitStatus;
            int pid = waitpid(-1, &childExitStatus, 0);
            sprintf(msg, "\nbackground pid %d is done: ", pid);
            if(WIFEXITED(childExitStatus)) {
                int exitStatus = WEXITSTATUS(childExitStatus);
                sprintf(msg + strlen(msg), "exit value %d\n", exitStatus);
            } else if(WIFSIGNALED(childExitStatus)) {
                int termSignal = WTERMSIG(childExitStatus);
                sprintf(msg + strlen(msg), "terminated by signal %d\n", termSignal);
            }
            write(STDOUT_FILENO, msg, sizeof(msg) - 1);
            break;
        }
        /* CTRL-C and SIGTERM both lead to child process exiting w/ status code 0 */
        case SIGINT:
        case SIGTERM:
            exit(0);
            break;
        default:
            break;
    }
}

/*******************************************************************************
 * void catchSignalBeforeWait()
 * 
 * Description: This function is the signal handler used when catching SIGTSTP 
 * before the current foreground process is done executing. It toggles the global
 * fgOnlyMode flag and updates the global SIGTSTPcaughtBeforeWait flag to 1 so the 
 * mode can be printed after the fg process is done executing (after waitpid).
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void catchSignalBeforeWait() 
{
    fgOnlyMode = !fgOnlyMode;
    SIGTSTPcaughtBeforeWait = 1;
}

/*******************************************************************************
 * void writeMode()
 * 
 * Description: This function writes to stdout the mode (entering/exiting fg only 
 * mode) the shell is now in based on the updated value of the global fgOnlyMode 
 * flag.
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void writeMode()
{
    char msg[MAX_CHARS_PATH];
    memset(msg, '\0', sizeof(msg));

    if(fgOnlyMode)
        strcpy(msg, "\nEntering foreground-only mode (& is now ignored)\n");
    else
        strcpy(msg, "\nExiting foreground-only mode\n");
    
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

/*******************************************************************************
 * void printExitStatus(int childExitStatus)
 * 
 * Description: This function outputs how the child process was finished based on the
 * exit status, which is either by executing the whole way through and exiting w/
 * some integer value or being terminated by a signal.
 * params:      childExitStatus (int)
 * ret:         N/A
 ******************************************************************************/
void printExitStatus(int childExitStatus)
{
    if(WIFEXITED(childExitStatus)) {
        int exitStatus = WEXITSTATUS(childExitStatus);
        printf("exit value %d\n", exitStatus);
    } else if(WIFSIGNALED(childExitStatus)) {
        int termSignal = WTERMSIG(childExitStatus);
        printf("terminated by signal %d\n", termSignal);
    }
    fflush(stdout);
}

/*******************************************************************************
 * void updateExitStatus(int childExitStatus, char** status)
 * 
 * Description: This function updates the given pointer to a str status based on 
 * the exit status of the child process, which will either be by executing the 
 * whole way through and exiting w/ some integer value or being terminated by a 
 * signal.
 * params:      childExitStatus (int)
 *              status (char**)
 * ret:         N/A
 ******************************************************************************/
void updateExitStatus(int childExitStatus, char** status)
{
    if(WIFEXITED(childExitStatus)) {
        int exitStatus = WEXITSTATUS(childExitStatus);
        sprintf(*status, "exit value %d", exitStatus);
    } else if(WIFSIGNALED(childExitStatus)) {
        int termSignal = WTERMSIG(childExitStatus);
        printf("terminated by signal %d\n", termSignal);
        fflush(stdout);
        sprintf(*status, "terminated by signal %d", termSignal);
    }
}

/*******************************************************************************
 * processes* initProcessesStruct()
 * 
 * Description: This function initializes the global processes struct, i.e. dynamically
 * allocates memory for the struct, sets all of the ids to -1 (invalid values) and 
 * the count to zero.
 * params:      N/A
 * ret:         bgProcesses (processes* - typedef'd struct declared in smallsh.h)
 ******************************************************************************/
processes* initProcessesStruct()
{
    processes* bgProcesses = (processes*)malloc(sizeof(processes));
    int i;
    for(i = 0; i < MAX_BG_PROCESSES; i++)
        bgProcesses->ids[i] = -1;
    bgProcesses->count = 0;
    return bgProcesses;
}

/*******************************************************************************
 * void killAll(processes* bgProcesses)
 * 
 * Description: This function kills all of the background processes as indicated 
 * by the passed in processes struct. It does not matter whether the processes are 
 * already terminated; just ensure that all processes started in the background 
 * are terminated before prog exit.
 * params:      bgProcesses (processes* - typedef'd struct declared in smallsh.h)
 * ret:         N/A
 ******************************************************************************/
void killAll(processes* bgProcesses)
{
    int i;
    for(i = 0; i < bgProcesses->count; i++)
        kill(bgProcesses->ids[i], SIGTERM);
}

/*******************************************************************************
 * void freeArgs(char** args)
 * 
 * Description: This function frees the memory dynamically allocated to the str 
 * arr args; called once done running the command.
 * params:      args (char**)
 * ret:         N/A
 ******************************************************************************/
void freeArgs(char** args)
{
    int i;
    for(i = 0; i < MAX_ARGS && args[i] != NULL; i++)
        free(args[i]); /* free memory allocated for each arg */
    free(args); /* free memory allocated for args */
}

/*******************************************************************************
 * void registerParentSIGTSTPactionBeforeWait()
 * 
 * Description: This function registers the parent SIGTSTP sig action, which should
 * be handled by the catchSignalBeforeWait signal handler that toggles the global
 * fgOnlyMode flag but delays printing whether the user is entering/exiting fg 
 * only mode until after the current fg process is done executing.
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void registerParentSIGTSTPactionBeforeWait()
{
    struct sigaction SIGTSTP_action = {{0}};
    SIGTSTP_action.sa_handler = catchSignalBeforeWait;
    sigfillset(&SIGTSTP_action.sa_mask); /* block/delay all signals while this mask is in place
                                            i.e. SIGTSTP being handled */
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

/*******************************************************************************
 * void defaultAction(const int SIGNAL)
 * 
 * Description: This function registers a sigaction that handles the given signal 
 * with the default behavior. 
 * params:      SIGNAL (const int)
 * ret:         N/A
 ******************************************************************************/
void defaultAction(const int SIGNAL)
{
    struct sigaction default_action = {{0}};
    default_action.sa_handler = SIG_DFL;
    sigaction(SIGNAL, &default_action, NULL);
}

/*******************************************************************************
 * void defaultAction(const int SIGNAL)
 * 
 * Description: This function registers a sigaction that ignores the given signal.
 * params:      SIGNAL (const int)
 * ret:         N/A
 ******************************************************************************/
void ignoreAction(const int SIGNAL)
{
    struct sigaction ignore_action = {{0}};
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGNAL, &ignore_action, NULL);
}

/*******************************************************************************
 * void registerParentSIGTSTPactionBeforeWait()
 * 
 * Description: This function registers the parent SIGTSTP handler, which does not
 * delay printing whether the user is entering/exiting fg only mode but immediately
 * does so after toggling the global fgOnlyMode flag.
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void registerParentSIGTSTPaction()
{
    /* SIGTSTP should be caught and handled by catchSignal unless caught while fg process is running */
    struct sigaction SIGTSTP_action = {{0}};
    SIGTSTP_action.sa_handler = catchSignal;
    sigfillset(&SIGTSTP_action.sa_mask); /* block/delay all signals while this mask is in place,
                                           i.e. SIGTSTP being handled */
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

/*******************************************************************************
 * void registerChildSIGINTaction()
 * 
 * Description: This function registers the child SIGINT action for processes run in
 * the foreground wherein they should terminate.
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void registerChildSIGINTaction()
{
    /* SIGINT should be caught and handled by catchSignal */
    struct sigaction SIGINT_action = {{0}};
    SIGINT_action.sa_handler = catchSignal;
    sigfillset(&SIGINT_action.sa_mask); /* block/delay all signals while this mask is in place,
                                           i.e. SIGINT being handled */
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
}

/*******************************************************************************
 * void registerChildSIGTERMaction()
 * 
 * Description: This function registers the child SIGTERM action for processes run in
 * the background wherein they should terminate.
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void registerChildSIGTERMaction()
{
    struct sigaction SIGTERM_action = {{0}};
    SIGTERM_action.sa_handler = catchSignal;
    sigfillset(&SIGTERM_action.sa_mask); /* block/delay all signals while this mask is in place,
                                           i.e. SIGTERM being handled */
    SIGTERM_action.sa_flags = 0;
    sigaction(SIGTERM, &SIGTERM_action, NULL);
}

/*******************************************************************************
 * void registerParentSIGCHLDaction()
 * 
 * Description: This function registers the parent SIGCHLD action wherein when a 
 * child terminates the parent should output what the child's pid is and how it 
 * finished (exited with value x or terminated by signal x).
 * params:      N/A
 * ret:         N/A
 ******************************************************************************/
void registerParentSIGCHLDaction()
{
    struct sigaction SIGCHLD_action = {{0}};
    SIGCHLD_action.sa_handler = catchSignal;
    sigfillset(&SIGCHLD_action.sa_mask); /* block/delay all signals while this mask is in place,
                                           i.e. SIGTSTP being handled */
    SIGCHLD_action.sa_flags = 0;
    sigaction(SIGCHLD, &SIGCHLD_action, NULL);
}

