/*
 * [Documentation]
 *
 * The University of Hong Kong
 * COMP3230 The Principles of operating systems (2022-23)
 * Programming Assignment #1
 * Name: Changjin Lee
 * UID: 3035435840
 * Development Platform: Written in VIM on workbench2 server
 * Compile: gcc -o mini_shell mini_shell.c
 * Execute: ./mini_shell
 * Remarks
     - I completed all the instructions including the bonus part (hope so).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <setjmp.h>
#include <errno.h>

// Signal description (from the tutorial COMP3230_signal.h)
char *signal_list[] = {
    "NULL",      // 0 - NULL, not used
    "SIGHUP",    // 1
    "SIGINT",    // 2
    "SIGQUIT",   // 3
    "SIGILL",    // 4
    "SIGTRAP",   // 5
    "SIGABRT",   // 6
    "SIGBUS",    // 7
    "SIGFPE",    // 8
    "SIGKILL",   // 9
    "SIGUSR1",   // 10
    "SIGSEGV",   // 11
    "SIGUSR2",   // 12
    "SIGPIPE",   // 13
    "SIGALRM",   // 14
    "SIGTERM",   // 15
    "SIGSTKFLT", // 16
    "SIGCHLD",   // 17
    "SIGCONT",   // 18
    "SIGSTOP",   // 19
    "SIGTSTP",   // 20
    "SIGTTIN",   // 21
    "SIGTTOU",   // 22
    "SIGURG",    // 23
    "SIGXCPU",   // 24
    "SIGXFSZ",   // 25
    "SIGVTALRM", // 26
    "SIGPROF",   // 27
    "SIGWINCH",  // 28
    "SIGIO",     // 29
    "SIGPWR",    // 30
    "SIGSYS",    // 31
};

char *signal_des[] = {
    "NULL",                     // 0 - NULL, not used
    "hangup",                   // 1
    "interrupt",                // 2
    "quit",                     // 3
    "illegal instruction",      // 4
    "trace trap",               // 5
    "abort",                    // 6
    "bus error",                // 7
    "floating point exception", // 8
    "kill",                     // 9
    "user defined signal 1",    // 10
    "segmentation violation",   // 11
    "user defined signal 2",    // 12
    "broken pipe",              // 13
};

// Constants
#define MAX_INPUT_CHAR 1025 // including \0
#define MAX_INPUT_STRING 30
#define TOTAL_CMD_NUM 1000 // total # of processes allowed before "exit"

static int fg_proc_num = 0;   // Count the # of foreground processes
static int SIGUSR1_READY = 0; // For SIGUSR1 signal to mark the child process to be ready

// To track {pid: cmd name} to acccess in the SIGCHLD handler
static int pid_list[TOTAL_CMD_NUM];
static char *cmd_name_list[TOTAL_CMD_NUM];
static int global_process_idx = 0;

// Various custom functions to handle parsing
int isAllSpaces(char *str);                                                                  // check if str consists of only spaces
int isEmpty(char *str);                                                                      // check if str is empty
char *trim(char *str);                                                                       // trim front/back spaces
char *substrFromZero(char *str, int len);                                                    // get substring str[0:len], len exclusive
int findTokenNum(char *input);                                                               // Find the # of tokens(or words). Ex) "ls -l -a" -> return 3
char *getFirstCommand(char *single_cmd);                                                     // Get the first token. Ex) "ls -l -a" -> return "ls"
char *getLastCmd(char *input, char delim);                                                   // Get the last token. Ex) "ls -l -a"" -> return "-a
int usetx_func(char *single_cmd);                                                            // Check if using TimeX command
char *removeTimeX(char *single_cmd);                                                         // Remove TimeX. Ex) "timeX ls -l -a" -> return "ls -l -a"
char **breakIntoTokens(char *cmd, int numToken, char **command);                             // break a command into 2D array of tokens.
char *getCmdNameByPid(int pid);                                                              // Get the command name by pid (defined as global variables)
char **parse_single_cmd(char *single_cmd, int *useTimeX, int *invalidTimeX, char **command); // parse "single" command. Ex) "ls -l -a"
char **parse_input(char *input, int *total_num_Cmds, int *errFlag);                          // parse the whole command line. Ex) "ls -l | grep sh"

// Functions to validate command line input
int checkInvalidExit(char **commands, int numCmds);                   // Check invalid exit usage
int checkStandaloneTimeX(char **commands, int numCmds);               // Check standalone timeX
int useTimeXBgTogether(char **commands, int totalNumCmds, int useBg); // Check using background and timeX together

// Functions to find if using background and timeX commands
char *useBackground(char *input, int *bgErrorFlag, int *useBg);
char *useTimeX(char *bg_parsed_input, int *usetx);

// Report(print) timeX results to the terminal
char *handle_timeX(struct rusage usage, pid_t child_pid, char *cmd);

// Signal handlers
void sigint_3230shell(int sig);
void sigusr1(int sig);
void sigchld(int sig);

int main(int argc, char *argv[])
{
    char comInput[MAX_INPUT_CHAR]; // read command line input

    // Register SIGINT handler
    signal(SIGINT, &sigint_3230shell);
    signal(SIGUSR1, &sigusr1);
    signal(SIGCHLD, &sigchld);

    while (1)
    {
        SIGUSR1_READY = 0;
        fg_proc_num = 0;
        memset(comInput, 0x00, sizeof(char) * (MAX_INPUT_CHAR - 1));

        // Read command line input
        printf("$$ COMP3230shell ## ");
        fgets(comInput, MAX_INPUT_CHAR, stdin);
        comInput[strlen(comInput) - 1] = 0x00; // set to NULL

        // Handle empty input (including all spaces)
        if (isEmpty(comInput) || isAllSpaces(comInput))
        {
            continue;
        }

        // Handle standalone & (same as handled by the standard bash shell)
        if (strlen(trim(comInput)) == 1 && trim(comInput)[0] == '&')
        {
            printf("3230shell: syntax error near unexpected token '&'\n");
            continue;
        }

        // Handle exit command - extra spaces after exit is allowed
        if (strcmp(trim(comInput), "exit") == 0)
        { // If input exit, then terminate the shell
            printf("3230shell: Terminated\n");
            return 0;
        }

        // Handle Background process
        int bgErrorFlag = 0;
        int useBg = 0;
        char *bg_parsed_input = useBackground(comInput, &bgErrorFlag, &useBg); // return trimmed

        if (bgErrorFlag)
        {
            printf("3230shell: '&' should not appear in the middle of the command line\n");
            continue;
        }

        // Check standalone timeX for the whole command line (that is, the input command line is only timeX)
        if (strcmp(getFirstCommand(bg_parsed_input), "timeX") == 0 && findTokenNum(bg_parsed_input) == 1)
        {
            printf("3230shell: \"timeX\" cannot be a standalone command\n");
            continue;
        }

        // Check using TimeX
        int usetx_global = 0;
        char *timeX_parsed_input = useTimeX(bg_parsed_input, &usetx_global);

        if (usetx_global && useBg)
        {
            printf("3230shell: \"timeX\" cannot be run in background mode\n");
            continue;
        }

        // Parse the command line to 2D array of separate sub-command (stored in commands)
        int totalNumCmds;
        int errFlag; // 1 for consecutive |, 0 for | in the front/back
        char **commands = parse_input(bg_parsed_input, &totalNumCmds, &errFlag);

        // Check for invalid pipe usage
        if (errFlag == 0)
        {
            printf("3230shell: should not have | as the first or last character\n");
            continue;
        }

        if (errFlag == 1)
        {
            printf("3230shell: should not have two consecutive | without in-between command\n");
            continue;
        }

        // Check for invalid exit
        if (checkInvalidExit(commands, totalNumCmds))
        {
            printf("3230shell: \"exit\" with other arguments!!!\n");
            continue;
        }

        // Check standalone timeX for all sub-commands (with pipes)
        // ex) ls -l | timeX | grep sh
        if (checkStandaloneTimeX(commands, totalNumCmds))
        {
            printf("3230shell: No Standalone timeX!\n");
            continue;
        }

        // Store timeX messages to print later
        char **timeXMessages = malloc(totalNumCmds * sizeof(char *));

        // Piping setup
        int in, out, fd[2];
        in = 0;
        out = 1;

        int idx;
        // Default: STDIN, STDOUT
        // 1st command: STDIN, fd[1]
        // 2nd command: fd[0], fd[1]
        // ...
        // last command: fd[0], STDOUT
        // So after the first command, the output should be redirected to fd[1]
        // Inbetween, both the input and output should be fd[0], fd[1] to pass along
        // In the last command, the output should be redirected to STDOUT again.
        for (idx = 0; idx < totalNumCmds; idx++)
        {
            // Handle command: firstCmd, argVec
            int usetx = 0, invalidtx = 0;
            char *firstCmd;
            char **argVec = parse_single_cmd(*(commands + idx), &usetx, &invalidtx, &firstCmd);

            fg_proc_num = 0;

            pipe(fd);
            pid_t pid = fork();

            if (pid == 0)
            {
                // Handle the background process
                if (useBg)
                {
                    setpgid(0, 0);
                }

                // Execute only upon the parent's SIGUSR1 signal
                while (1)
                {
                    if (SIGUSR1_READY == 1)
                        break;
                    usleep(10);
                }

                // Whatever the "in" currently is, the STDIN should be linked to it.
                // So our job is to set the proper value for "in" for every loop
                dup2(in, 0);

                // Except for the last command, the output should be redirected to fd[1]
                if (idx < totalNumCmds - 1)
                {
                    dup2(fd[1], 1);
                }

                // Close file descriptors
                close(fd[1]);
                close(fd[0]);

                // Execute command
                int err = execvp(firstCmd, argVec);
                if (err == -1)
                {
                    printf("3230shell: %s: %s\n", firstCmd, strerror(errno));
                    return -1;
                }
            }
            else
            {
                // Update the {pid: cmd name} list for handling the background process later
                pid_list[global_process_idx] = (int)pid;
                cmd_name_list[global_process_idx] = firstCmd;
                global_process_idx++;

                // send SIGUSR1 signal to make the child process "ready"
                kill(pid, SIGUSR1);
                fg_proc_num++;

                // For timeX statistics report
                struct rusage usage;

                // Handling the parent process
                int status;
                pid_t child_pid;
                if (useBg == 0)
                {
                    child_pid = wait4((pid_t)-1, &status, 0, &usage);

                    if (WIFEXITED(status))
                    {
                        // If using timeX, save the timeX stats to report alltogether later
                        if (usetx_global)
                        {
                            char *single_tx_msg = handle_timeX(usage, child_pid, firstCmd);
                            *(timeXMessages + idx) = malloc((strlen(single_tx_msg) + 1) * sizeof(char));
                            strcpy(*(timeXMessages + idx), single_tx_msg);
                            timeXMessages[idx][strlen(single_tx_msg)] = '\0';
                        }
                        int statusCode = WEXITSTATUS(status);

                        if (statusCode == 0)
                        {
                            /** printf("Success!\n"); */
                        }
                        else
                        {
                            /** printf("Faillure!\n"); */
                        }
                    }
                    else if (WIFSIGNALED(status))
                    { // Handling the child process terminated by a signal
                        int sigNum = WTERMSIG(status);
                        char *term_sig_desc = signal_des[sigNum];
                        printf("%s\n", term_sig_desc);
                    }
                    else
                    {
                    }

                    close(fd[1]);

                    // redirected to fd[0] for the next command
                    in = fd[0];
                }
            }
        }

        // Report timeX statistics
        if (usetx_global)
        {
            for (int i = 0; i < totalNumCmds; i++)
            {
                printf("%s\n", timeXMessages[i]);
            }
        }

        if (useBg == 0)
        {
            close(fd[0]);
            close(fd[1]);
        }
    }
}

int isAllSpaces(char *str)
{
    /*
     * Return: 1 if all spaces, 0 otherwise
     */
    if (strlen(str) == 0)
        return 0;
    int len = strlen(str);

    for (int i = 0; i < len; i++)
    {
        if (str[i] != ' ')
            return 0;
    }
    return 1;
}

int isEmpty(char *str)
{
    return strlen(str) == 0;
}

char *trim(char *str)
{
    /*
     * Return: trimmed string (spaces at front/back removed)
     * */
    if (isEmpty(str) || isAllSpaces(str))
        return str; // if empty or consists of only spaces, return it

    int len = strlen(str);
    int i = 0, j = len - 1;

    while (str[i] == ' ')
        i++;
    while (str[j] == ' ')
        j--;

    char *temp = malloc((j - i + 2) * sizeof(char));
    temp[j - i + 1] = '\0';
    strncpy(temp, str + i, j - i + 1);

    return temp;
}

// Return str[0: len], len exclusive
char *substrFromZero(char *str, int len)
{
    char *temp = malloc((len + 1) * sizeof(char));
    strncpy(temp, str, len);
    temp[len] = '\0';
    return temp;
}

// Find the # of tokens
// ex) "ls -l -a" -> return 3
int findTokenNum(char *input)
{
    int token_num = 1; // total # tokens = 1 + # of spaces
    int i = 0;
    while (input[i] != '\0')
    {
        if (input[i] == ' ')
            token_num++;
        i++;
    }

    return token_num;
}

// "ls -l -a" -> return "ls"
char *getFirstCommand(char *single_cmd)
{
    char *breakPoint = strstr(single_cmd, " ");
    char *start = single_cmd;
    int len;
    char *firstCommand;
    if (breakPoint == NULL)
    { // single word
        len = strlen(single_cmd);
    }
    else
    {
        len = breakPoint - start;
    }
    firstCommand = substrFromZero(single_cmd, len);
    return firstCommand;
}

// Return simply wheter use timeX or not
int usetx_func(char *single_cmd)
{
    // char* single_cmd: "timeX ls -l -a" or "ls -l -a"
    char *firstCommand = getFirstCommand(trim(single_cmd));
    return (strcmp(firstCommand, "timeX") == 0);
}

// If use TimeX, return command after TimeX
// It's already guaranteed at this point that it's a proper timeX-including command with multiple tokens
char *removeTimeX(char *single_cmd)
{
    // "timeX ls -l" -> "ls -l"
    return single_cmd + 6;
}

// Convert an input to 2D array of tokens
// Ex) "ls -l -a " -> {"ls", "-l", "-a"}
char **breakIntoTokens(char *cmd, int numToken, char **command)
{
    // char* cmd: "ls -l -a"
    // int numToken: 3
    // char** command: call by reference

    char **tokens = malloc((numToken + 1) * sizeof(char *));

    char *breakPoint = strstr(cmd, " ");
    char *start = cmd;
    int len;
    int i = 0;

    while (breakPoint != NULL)
    {
        len = breakPoint - start;
        tokens[i] = malloc((len + 1) * sizeof(char));
        strncpy(tokens[i], start, len);
        tokens[i][len] = '\0';
        start += (len + 1);
        /** printf("tokens[%d]: %s\n", i, tokens[i]); */
        i++;
        breakPoint = strstr(start, " ");
    }

    int totalLen = strlen(cmd);
    int lastCmdLen = (cmd + totalLen) - start;
    char *lastCmd = substrFromZero(start, lastCmdLen);

    tokens[i] = malloc((lastCmdLen + 1) * sizeof(char));
    strcpy(tokens[i], lastCmd);
    tokens[i][lastCmdLen] = '\0';
    /** printf("tokens[%d]: %s\n", i, tokens[i]); */

    tokens[numToken] = '\0';
    *command = getFirstCommand(cmd);

    return tokens;
}

// Parse a single sub-command. Ex) "ls -l -a"
char **parse_single_cmd(char *single_cmd, int *useTimeX, int *invalidTimeX, char **command)
{
    /*
    Call by reference:
        int* useTimeX: 0 or 1
        int* invalidTimeX: 0 or 1
        char** command
    Returns:
        2D array of tokens
    */
    int tokenNum = findTokenNum(single_cmd);
    int removedTimeXTokenNum = tokenNum;
    int usetx = usetx_func(single_cmd);

    // If using timeX
    if (usetx)
    {
        *useTimeX = 1; // set timeX flag to 1
        if (tokenNum == 1)
        { // standalone timeX error
            *invalidTimeX = 1;
            return NULL;
        }

        // Parse single_cmd by removing timeX
        single_cmd = removeTimeX(single_cmd); // ex) "ls -l"
        removedTimeXTokenNum--;               // removedTimeXTokenNum = 2
    }

    char **tokens = breakIntoTokens(single_cmd, removedTimeXTokenNum, &*command);

    return tokens;
}

// Parse the whole command line. Ex) "ls -l -a | grep sh"
char **parse_input(char *input, int *total_num_Cmds, int *errFlag)
{
    // The input must be already parsed for timeX (timeX must be removed in prior and handled separately)
    // errFlag = 0: invalid usage of | (as a first or last character of the input)
    // errFlag = 1: two consecutive ||
    // If both errFlag 0 and errFlag 1, report errFlag 0

    // trim input (remove spaces front and back)
    *errFlag = -1; // no error
    input = trim(input);
    char *start = input;
    char *breakPoint = strstr(input, "|");
    char **output = malloc(1 * sizeof(char *));
    int numCmds = 0;
    int i = 0;
    int len;
    int invalidFlag = 0;
    int invalidConsecutive = 0;

    // Invalid Usage: | at the front or back
    if (input[0] == '|' || input[(int)(strlen(input) - 1)] == '|')
    {
        *errFlag = 0;
        return NULL;
    }

    while (breakPoint != NULL)
    {
        len = breakPoint - start;
        char *temp = substrFromZero(start, len);

        // If the in-between token is empty, then it's using two consecutive ||s
        if (isEmpty(temp) || isAllSpaces(temp))
        {
            *errFlag = 1;
            return NULL;
        }

        char *trimmed_cmd = trim(temp);
        int trimmed_len = (int)strlen(trimmed_cmd);

        // Store command into output
        output[i] = malloc((trimmed_len + 1) * sizeof(char));
        strcpy(output[i], trimmed_cmd);
        output[i][trimmed_len] = '\0'; // mark the end of string

        start += (len + 1); // jump to next start of the command

        numCmds++;
        // Reallocate one more memory block for each loop
        output = realloc(output, (numCmds + 1) * sizeof(char *));

        breakPoint = strstr(start, "|");
        /** printf("%s,%d\n", output[i], (int)strlen(output[i])); */
        i++;
    }

    // Handle the last command
    int totalLen = strlen(input);
    int lastCmdLen = (input + totalLen) - start;
    char *last_temp = substrFromZero(start, lastCmdLen);
    char *trimmed_last_cmd = trim(last_temp);
    int trimmed_last_cmd_len = (int)strlen(trimmed_last_cmd);
    output[i] = malloc((trimmed_last_cmd_len + 1) * sizeof(char));
    strcpy(output[i], trimmed_last_cmd);
    output[i][trimmed_last_cmd_len] = '\0';
    numCmds++;

    // Also record the total number of commands
    *total_num_Cmds = numCmds;

    return output;
}

// Return timeX statistics
char *handle_timeX(struct rusage usage, pid_t child_pid, char *cmd)
{
    // Extract only command name
    char *token = strtok(cmd, "/");
    char *last_cmd = token;

    while (token != NULL)
    {
        strcpy(last_cmd, token);
        token = strtok(NULL, "/");
    }

    // Handle user/sys time
    int user_sec = usage.ru_utime.tv_sec;
    int user_usec = usage.ru_utime.tv_usec;
    int sys_sec = usage.ru_stime.tv_sec;
    int sys_usec = usage.ru_stime.tv_usec;
    float utime = (float)user_sec + (0.000001) * (float)user_usec;
    float stime = (float)sys_sec + (0.000001) * (float)sys_usec;

    // Report result
    char *txMessage = malloc(100 * sizeof(char));

    snprintf(txMessage, 100, "(PID)%d  (CMD)%s    (user)%.3f s  (sys)%.3f s", child_pid, last_cmd, utime, stime);
    return txMessage;
}

// Check invalid exit command usage
int checkInvalidExit(char **commands, int numCmds)
{
    /*
     flag 0: ALL valid commands
     flag 1: invalid exit usage
    */
    for (int i = 0; i < numCmds; i++)
    {
        char *single_cmd = *(commands + i);             // "ls -l a"
        char *firstToken = getFirstCommand(single_cmd); // "ls"
        int numTokens = findTokenNum(single_cmd);

        if (strcmp(firstToken, "exit") == 0 && numTokens > 1)
            return 1;
    }

    return 0;
}

// Check if using standalone timeX command (which is not allowed)
int checkStandaloneTimeX(char **commands, int numCmds)
{
    for (int i = 0; i < numCmds; i++)
    {
        char *single_cmd = *(commands + i);
        char *firstToken = getFirstCommand(single_cmd);
        int numTokens = findTokenNum(single_cmd);

        if (strcmp(firstToken, "timeX") == 0 && numTokens == 1)
            return 1;
    }

    return 0;
}

// Check if using background process
char *useBackground(char *input, int *bgErrorFlag, int *useBg)
{
    // "   ls | grep cs  &  " -> "ls | grep cs"
    char *trimmed_input = trim(input); // "ls | grep cs &"
    int trimmed_len = strlen(trimmed_input);

    // Invalid background usage
    for (int i = 0; i < trimmed_len; i++)
    {
        if (trimmed_input[i] == '&' && i != trimmed_len - 1)
        {
            *bgErrorFlag = 1;
            return NULL;
        }
    }

    // Use background
    if (trimmed_input[trimmed_len - 1] == '&')
    {
        // "ls | grep cs &"
        /** parsed = malloc((trimmed_len) * sizeof(char)); */
        trimmed_input = substrFromZero(trimmed_input, trimmed_len - 1);
        trimmed_input[trimmed_len - 1] = '\0';
        *useBg = 1;
    }

    return trim(trimmed_input);
}

// Check if using timeX and $ together (which is not allowed)
int useTimeXBgTogether(char **commands, int totalNumCmds, int useBg)
{
    if (!useBg)
        return 0;
    for (int i = 0; i < totalNumCmds; i++)
    {
        char *cmd = *(commands + i);
        if (usetx_func(cmd))
            return 1;
    }
    return 0;
}

// Check if using timeX.
// If yes, then return the input with "timeX" removed
char *useTimeX(char *bg_parsed_input, int *usetx)
{
    if (usetx_func(bg_parsed_input))
    {
        *usetx = 1;
        return removeTimeX(bg_parsed_input);
    }

    return bg_parsed_input;
}

// Handle SIGINT signal for the 3230shell process.
// Should ignore Ctrl+C
void sigint_3230shell(int sig)
{
    if (fg_proc_num == 0)
    {
        printf("\n$$ COMP3230shell ## ");
        fflush(stdout);
    }
}

// Handle SIGUSR1 signal -> set "ready" to 1 so that the child process could begin
void sigusr1(int sig)
{
    SIGUSR1_READY = 1;
}

// Get the command name by PID (lists defined as global variables)
char *getCmdNameByPid(int pid)
{
    int loc;
    for (int i = 0; i < global_process_idx; i++)
    {
        if (pid_list[i] == pid)
        {
            loc = i;
            break;
        }
    }

    return cmd_name_list[loc];
}

// "ls -l -a" -> return "-a"
char *getLastCmd(char *input, char delim)
{
    // For example, input: "./A/B/C/loop_3" or just "loop_3" -> return "loop_3"
    int len = strlen(input);
    int delim_loc = -1;

    // Find the rightmost delim location
    for (int i = len - 1; i >= 0; i--)
    {
        if (input[i] == delim)
        {
            delim_loc = i;
            break;
        }
    }

    if (delim_loc == -1)
        return input;
    return input + delim_loc + 1;
}

// Handle SIGCHLD signal for background processes
void sigchld(int sig)
{
    siginfo_t sig_info;
    int status;

    while (waitid(P_ALL, 0, &sig_info, WEXITED | WNOWAIT | WNOHANG) >= 0)
    {
        pid_t pid = sig_info.si_pid;
        if (pid == 0)
        {
            break;
        }

        char *cmd_name = getCmdNameByPid((int)pid);
        char *last_cmd = getLastCmd(cmd_name, '/'); // Get the rightmost name to report
        fflush(stdout);

        // Remove Zombie processes
        waitpid(pid, &status, 0);

        // If the background child process exited
        if (WIFEXITED(status))
        {
            int statusCode = WEXITSTATUS(status);

            if (statusCode == 0)
            {
                printf("\n[%d] %s Done\n", pid, last_cmd);
            }
            else
            {
                // background process failure
            }
        }
        // If the background child process exited by a signal
        else if (WIFSIGNALED(status))
        {
            int sigNum = WTERMSIG(status);
            char *term_sig_desc = signal_des[sigNum];
            printf("\n[%d] %s Terminated\n", pid, last_cmd);
            printf("%s\n", term_sig_desc);
        }
    }
}
