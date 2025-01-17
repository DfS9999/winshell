#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <windows.h>

//#define DEBUG
#define INPUT_BUFFER_SIZE   512
#define TOKEN_BUFFER_SIZE   INPUT_BUFFER_SIZE / 16

// INPUT GLOBAL STRUCT
struct {
    char buffer[INPUT_BUFFER_SIZE];
    char command[INPUT_BUFFER_SIZE];
} I;

// TOKENS GLOBAL STRUCT
struct {
    size_t count;
    char* tokens[TOKEN_BUFFER_SIZE];
} T;

// built-in command declarations
const char* builtInCommandStrs[] = { "help", "cd", "pwd", "exit" };
void BIHelp(void);
void BICd(void);
void BIPwd(void);
void BIExit(bool*);
void* builtInCommands[] = {
    (void*)BIHelp,
    (void*)BICd,
    (void*)BIPwd,
    (void*)BIExit
};

void Tokenize(void);
void AssembleCommand(void);
bool CreateProcessAndExecute(void);
void TranslateErrorCode(DWORD code);
int  IsBuiltIn(void);
char* myGetCurrentDirectory(void);

int main(void) {
    HANDLE shell = GetCurrentProcess();
    DWORD shellPID = GetProcessId(shell);

    bool running = true;
    printf("---- SHELL (PID: %ld) ----\n", shellPID);
    while (running) {
        memset(I.buffer, 0, INPUT_BUFFER_SIZE);
        memset(I.command, 0, INPUT_BUFFER_SIZE);

        printf("command: ");
        char* ptrfgets = fgets(I.buffer, INPUT_BUFFER_SIZE, stdin);    // reads input into I buffer
        if (ptrfgets == NULL || strlen(I.buffer) < 1 || I.buffer[0] == '\n') { continue; }

        Tokenize();                                     // save input buffer into T tokens

        int cmdIdx = IsBuiltIn();                       // check if it is built in command
        if (cmdIdx != -1) {
//// TODO create a built-function struct to avoid idx checking and fptr casting
            if (cmdIdx == 3) { 
                ( (void(*)(bool*)) (builtInCommands[cmdIdx]) )(&running); // exit function call
            }
            else {
                ((void(*)())(builtInCommands[cmdIdx]))();       // execute as built-in command //void(*)() types
            }
        } else {
            AssembleCommand();                          // or assemble command from tokens
            if (CreateProcessAndExecute())              // and execute with child process
                printf("Command executed successfully.\n");
        }
    }

    return 0;
}

// built-in help
void BIHelp(void) {
    printf("\nBuilt in commands are as follows: ");
    for (size_t i = 0; i < (sizeof(builtInCommandStrs)/sizeof(char*)); i++) { printf("%s ", builtInCommandStrs[i]); }
    printf("\nOtherwise the provided command gets wrapped into 'cmd /C', if not an executable.\n\n");
}

// built-in Change Directory
void BICd(void) {
    if (T.count > 1) {
        if (SetCurrentDirectory(T.tokens[1])) { printf("Directory has been changed.\n"); }
        else { TranslateErrorCode(GetLastError()); }
    } else {
        fprintf(stderr, "Provide paramater to Change Directory!\n");
    }
}

// built-in Print Working Directory
void BIPwd(void) {
    char* currentDir = myGetCurrentDirectory();
    if (currentDir) {
        printf("Current Directory: %s\n", currentDir);
        free(currentDir);
    } else {
        fprintf(stderr, "Printing working directory failed.\n");
    }
}

void BIExit(bool* running) {
    printf("Exiting...\n");
    *running = false;
}

int IsBuiltIn(void) {
    for (size_t i = 0; i < (sizeof(builtInCommandStrs)/sizeof(char*)); i++)
        if (strcmp(T.tokens[0], builtInCommandStrs[i]) == 0)
            return (int)i;
    return -1;
}

char* myGetCurrentDirectory(void) {
    DWORD currDirBufferLen = 128;           // length to save current directory name
    char* currentDir = malloc(sizeof(char) * currDirBufferLen);
    if (!currentDir) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }
    DWORD currDirLen = GetCurrentDirectory(currDirBufferLen, currentDir);
    if (currDirBufferLen < currDirLen + 1) {
        currDirBufferLen = currDirLen + 1;
        char* temp = realloc(currentDir, sizeof(char) * currDirBufferLen);
        if (!temp) {
            fprintf(stderr, "Memory reallocation failed\n");
            free(currentDir);
            return NULL;
        } else {
            currentDir = temp;
        }
        GetCurrentDirectory(currDirBufferLen, currentDir);
    }
    return currentDir;
}

bool CreateProcessAndExecute(void) {
    STARTUPINFO si;                         // window related information for the new process
    PROCESS_INFORMATION pi;                 // new process's data
    SecureZeroMemory(&si, sizeof(si));      // windows.h zeroing function
    si.cb = sizeof(si);
    SecureZeroMemory(&pi, sizeof(pi));
    DWORD exitCode;

    char* currentDir = myGetCurrentDirectory(); // getting current directory for child process
    if (!currentDir) {
        fprintf(stderr, "myGetCurrentDirectory failed.\n");
        return false;
    }

    char* environment = GetEnvironmentStrings(); // environment strs for child process
    if (!environment) {
        TranslateErrorCode(GetLastError());
        free(currentDir);
        return false;
    }

    if(!CreateProcess(NULL,               // No module name (use command line)
                      I.command,          // Command line
                      NULL,               // Process handle not inheritable
                      NULL,               // Thread handle not inheritable
                      FALSE,              // Set handle inheritance to FALSE
                      0,                  // No creation flags
                      environment,        // Use parent's environment block
                      currentDir,         // Use parent's starting directory
                      &si,                // Pointer to STARTUPINFO structure
                      &pi)                // Pointer to PROCESS_INFORMATION structure
    ) {
        TranslateErrorCode(GetLastError());
        free(currentDir);
        FreeEnvironmentStrings(environment);
        return false;
    }
    else {
        // process spawned
        printf("Created PID: %ld\n", pi.dwProcessId);

        DWORD wait = WaitForSingleObject(pi.hProcess, INFINITE);
        if (wait == WAIT_FAILED) {
            TranslateErrorCode(GetLastError());
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            free(currentDir);
            FreeEnvironmentStrings(environment);
            return false;
        }

        GetExitCodeProcess(pi.hProcess, &exitCode);
    }

    // cleanup
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    free(currentDir);
    FreeEnvironmentStrings(environment);

    return exitCode == 0;
}

void TranslateErrorCode(DWORD code) {
    char* buffer = NULL;
    int success = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, code, 0, (char*)&buffer, 0, NULL); // buffer cast: FORMAT_MESSAGE_ALLOCATE_BUFFER flag (polymorphism hack)

    if (success) { printf("Error (%ld): %s", code, buffer); }
    else { fprintf(stderr, "Error code (%ld) unresolved! (Error: %ld).\n", code, GetLastError()); }

    LocalFree(buffer);                  // win. localfree for freeing buffer allocated by FormatMessage
}

void AssembleCommand(void) {
    // if not .exe / .bat, adding 'cmd /C'
    bool isExecutableOrBatchFile = false;
    const char* extensions[] = { ".exe", ".bat" };
    size_t comLen = strlen(T.tokens[0]);
    for (size_t i = 0; i < (sizeof(extensions) / sizeof(extensions[0])); i++) {
        size_t extLen = strlen(extensions[i]);
        if (extLen >= comLen) { continue; }
        else if (strcmp(T.tokens[0] + comLen - extLen, extensions[i]) == 0) {
            isExecutableOrBatchFile = true;
            break;
        }
    }

    // if not an exe or bat, save the prefix into at the start of I.command buffer
    const char prefix[] = "cmd /C ";
    if (isExecutableOrBatchFile == false) { strcpy(I.command, prefix); }
    if (isExecutableOrBatchFile == false && strlen(I.buffer) >= INPUT_BUFFER_SIZE - strlen(prefix) - 1) {
        fprintf(stderr, "Too long command provided!\n");
        return;
    }

    // Assembling and saving command into the I.command buffer
    for (size_t i = 0;; i++) {
        strcat(I.command, T.tokens[i]);
        if (i + 1 == T.count) break;
        else { strcat(I.command, " "); }
    }
}

void Tokenize(void) {
    // zeroing the current tokens
    for (size_t i = 0; i < TOKEN_BUFFER_SIZE; T.tokens[i++] = NULL);
    T.count = 0;

    // split buffer by spaces, and save the beginnings
    char* p = I.buffer;
    while (*p != '\0' && *p != '\n' && p < (I.buffer + INPUT_BUFFER_SIZE) && T.count < TOKEN_BUFFER_SIZE) {
        // skipping spaces
        while (*p == ' ') { p++; }
        // start of the word
        if (*p != '\0' && *p != '\n') {
            // saving address
            T.tokens[T.count++] = p;
            // inside the word
            while (*p != ' ' && *p != '\0' && *p != '\n') { p++; }
            // null terminate end
            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }
    }
#ifdef DEBUG
        printf("Read in %zu tokens:\n", T.count);
        for (size_t c = 0; c < T.count; c++) {
            printf("%zu.token=%s; length=%zu\n", c+1, T.tokens[c], strlen(T.tokens[c]));
        }
#endif
}

