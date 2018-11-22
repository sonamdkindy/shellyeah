/*******************************************************************************
 * Sonam Kindy
 * Description: shellyeah (header file)
 * Created:     11/4/18
 ******************************************************************************/

#ifndef _KINDYS_SHELLYEAH
#define _KINDYS_SHELLYEAH
	#define MAX_BG_PROCESSES        1024
	#define MAX_CL_CHARS            2048 + 2    /* room for new line and null terminator */
	#define MAX_ARGS                512
	#define MAX_CHARS_PATH          256

	typedef struct {
	    int ids[MAX_BG_PROCESSES];
	    int count;
	} processes;

	processes* initProcessesStruct();

	void prompt();
	void reapZombies();
	void dispatch(char* commandLine, char** status);
	void execCommand(char* command, char* argsArr[MAX_ARGS], char* infile, 
                     char* outfile, int* isBgProcess, char** status);
	void changedir(char* path);
	void killAll(processes* bgProcesses);
	void replaceStr(char* str, char* substr, char* replacement);

	char** getArgs(char* commandLine, int* bgProcess);
	void freeArgs(char** argsArr);

	void printExitStatus(int childExitStatus);
	void updateExitStatus(int childExitStatus, char** status);

	void writeMode();
	void catchSignalBeforeWait();
	void catchSignal(int signo);

	void registerParentSIGTSTPactionBeforeWait();
	void registerParentSIGTSTPaction();
	void registerParentSIGCHLDaction();
	void registerChildSIGINTaction();
	void registerChildSIGTERMaction();
	void defaultAction(const int SIGNAL);
	void ignoreAction(const int SIGNAL);
#endif
	
