#define _XOPEN_SOURCE 700 // Needed to create the sigaction struct for background processes
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>

// Program constants
#define MAX_LINE 80 // Max len for cmd line input
#define MAX_DIR 249 // Max len for directory path, set to 249 to account for the 6 extra prompt chars (1 byte of chars)
#define MAX_HISTORY 5 // Max len of num of cmds in history

// Global variables
struct termios canon, noncanon; // Structs to change terminal modes
char history[MAX_HISTORY][MAX_LINE]; // Array of cmd history
int history_count = 0; // Number of cmd in history
int history_index = 0; // Index of current cmd in history

// Handler for SIGCHLD signal, which cleans up background processes
void sigchld(int sid) {
	// Waits for background processeses to finish and cleans them up
	// Needed so the parent process isn't blocked while waiting for background processes to finish
	while (waitpid((pid_t)-1, NULL, WNOHANG) > 0);
}

// Navigates cmd history when the up/down arrow keys are pressed
void get_history(char* prompt, char* chars, int* index) {
	// Array to store a cleared line, 3 is added to account for 2 \r chars and \0 char since we're clearing the entire line
	// We clear to replace the current line with the cmd from history
	char clear_line[MAX_LINE + 3];
	snprintf(clear_line, sizeof(clear_line), "\r%*s\r", MAX_LINE, ""); // Formats the cleared line to be printed
	write(STDOUT_FILENO, clear_line, strlen(clear_line));  // Prints the cleared line
	write(STDOUT_FILENO, prompt, strlen(prompt)); // Re-prints the prompt
	strcpy(chars, history[history_index]); // Copies the cmd from the current history index to chars
	*index = strlen(chars); // Updates index to align with the new chars len
	write(STDOUT_FILENO, chars, *index); // Prints the updated chars array with the navigated-to cmd
}

// Reads user input char-by-char with non-canonical terminal mode
char* noncanon_input(char* prompt, char* chars) {
	int index = 0; // Index of current char in chars
	char c; // Current char being read
	memset(chars, 0, MAX_LINE); // Clears chars
	history_index = history_count; // Sets history index to the end of the history

	// Read input while there's space in chars
	while (index < MAX_LINE - 1) {
		read(STDIN_FILENO, &c, 1); // Reads char the user inputted and stores it in c
		if (c == 10) { // Newline char \n inputted, input is complete and stop looping
			break;
		} else if (c == 127) { // Backspace char \b inputted
			if (index > 0) { // If there's a char to delete, delete it
				index--; // Dec index to align it with the new cursor position
				write(STDOUT_FILENO, "\b \b", 3); // Moves cursor back, prints a space, and moving cursor back again to delete the char
			}
		} else if (c == 27) { // Arrow key escape sequence inputted
			char seq[3]; // Stores the escape sequence (like ['[', 'A', '\0'])
			read(STDIN_FILENO, &seq[0], 1);
			read(STDIN_FILENO, &seq[1], 1);
			seq[2] = '\0';

			if (strcmp(seq, "[A") == 0) { // Up arrow to navigate to more previous cmd in history
				if (history_count > 0 && history_index > 0) {
					history_index--; // Dec history index to align with more previous cmd
					get_history(prompt, chars, &index); // Navigate to more previous cmd in history
				}
			} else if (strcmp(seq, "[B") == 0) { // Down arrow to navigate to more recent cmd in history
				if (history_count > 0 && history_index < history_count - 1) { // If there's a cmd in history and the current cmd isn't the most recent
					history_index++; // Inc history index to align with more recent cmd
					get_history(prompt, chars, &index); // Navigate to more recent cmd in history
				} else if (history_index == history_count - 1) { // If the current cmd is the most recent in history
					history_index = history_count; // Set history index to the end of the history
					char clear_line[MAX_LINE + 3]; // Array to store the cleared line
					snprintf(clear_line, sizeof(clear_line), "\r%*s\r", MAX_LINE, ""); // Clears the current line
					write(STDOUT_FILENO, clear_line, strlen(clear_line)); // Prints the cleared line
					write(STDOUT_FILENO, prompt, strlen(prompt)); // Prints the prompt
					memset(chars, 0, MAX_LINE); // Clears chars
					index = 0; // Resets the index to 0
				}
			}
		} else {
			chars[index++] = c; // Add the typed char to chars and inc index to align with the new cursor position
			write(STDOUT_FILENO, &c, 1); // Print the char to the terminal
		}
	}
	chars[index] = '\0'; // Null-terminate chars
	write(STDOUT_FILENO, "\n", 1); // Print newline
	return chars; // Returns chars with the input to be put into the input array
}

void print_prompt(char* prompt, int len_prompt) {
	char dir[MAX_DIR]; // Array to store directory path
	getcwd(dir, sizeof(dir)); // Get current working directory
	char* dirPtr; // Pointer to directory path
	if (strrchr(dir, '/') == NULL) { // If no '/' in the directory path, set dirPtr to dir
		dirPtr = dir;
	} else { // If the directory path contains '/', set dirPtr to char after last '/'
		dirPtr = strrchr(dir, '/');
		dirPtr++;
	}
	snprintf(prompt, len_prompt, "osh:%s> ", dirPtr); // Format terminal prompt
	write(STDOUT_FILENO, prompt, strlen(prompt)); // Print prompt, using write instead of printf to get immediate output without fflush
}

// If input is "exit", terminate program
int input_exit(char* input, int* should_run) {
	if (strcmp(input, "exit") == 0) {
		*should_run = 0; // Set should_run to 0 to stop the program loop
		return 1;
	}
	return 0;
}

// If input is "!!", repeat most previous cmd in history
int input_prev(char *input) {
	if (strcmp(input, "!!") == 0) {
		if (history_count == 0) { // If there's no previous cmd in history, print error
			printf("No commands in history.\n");
			return 1; // Restart program loop due to input error
		}
		strcpy(input, history[history_count - 1]); // Copy the most recent cmd in history to input
		printf("Previous command: \"%s\"\n", input); // Shows user their most recent cmd that's being re-executed
	}
	return 0;
}

// If input is "history", print command history
int input_history(char *input) {
	if (strcmp(input, "history") == 0) {
		for (int i = 0; i < history_count; i++) {
			printf("%d\t%s\n", i, history[i]);
		}
		return 1; // Restart program loop after displaying history
	}
	return 0;
}

void add_history(char* input) {
	// Add input to history before parsing
	if (history_count < MAX_HISTORY) { // If history isn't full, simply add input to history
		strcpy(history[history_count], input); // Copy input to history and inc history count
		history_count++;
	} else { // If history is full, shift up all cmds and add input to the end
		for (int i = 1; i < MAX_HISTORY; i++) {
			strcpy(history[i - 1], history[i]);
		}
		strcpy(history[MAX_HISTORY - 1], input); // Copy input to the end of history
	}
}

// Parse input
void input_parse(char* input, char** args, int* bkgd, char** inpRedirFile, char** outpRedirFile, int* pipe_index) {
	int token_index = 0; // Index of current token in args
	char* token = strtok(input, " "); // Ptr to first token in input, each token's obtained through space separation
	while (token != NULL) { // While the input has tokens
		if (strcmp(token, "&") == 0) { // If token is "&", set background flag
			*bkgd = 1;
		} else if (strcmp(token, "<") == 0) { // If token is "<", set input redirection file to the next token
			token = strtok(NULL, " ");
			*inpRedirFile = token;
		} else if (strcmp(token, ">") == 0) { // If token is ">", set output redirection file to the next token
			token = strtok(NULL, " ");
			*outpRedirFile = token;
		} else if (strcmp(token, "|") == 0) { // If token is "|", set pipe index and null-terminate pipe index
			*pipe_index = token_index;  // Store pipe index
			args[token_index] = NULL;  // Null-terminate pipe index
			token_index++; // Inc token index to align with the next arg
		} else { // If token isn't an I/O redirector or background symbol, add it to the args array
			args[token_index] = token;
			token_index++;
		}
		token = strtok(NULL, " "); // Get next token
	}
	args[token_index] = NULL; // Make the final char in args null for termination
}

int input_cd(char** args) {
	// If input is "cd", change directory
	if (strcmp(args[0], "cd") == 0) { // If the first arg is "cd"
		if (args[1] == NULL) { // If no directory is given, print error
			printf("Error: \"cd\" requires a directory\n");
		} else if (chdir(args[1]) != 0) { // If directory isn't found, print error
			printf("Error: \"%s\" is not a recognized directory\n", args[1]);
		}
		return 1; // Restart program loop after running cd since it doesn't use execvp
	}
	return 0;
}

int main(int argc, char** argv) {
	int should_run = 1; // Flag to run program loop, becomes 0 when user types exit, prompting program to exit
	char* args[MAX_LINE]; // Array to store cmd args
	tcgetattr(STDIN_FILENO, &canon); // Get canonical terminal attributes (normal terminal mode where input ends after newline)
	noncanon = canon; // Copy canonical terminal attributes to noncanonical
	noncanon.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo so input's processed immediately but not automatically printed
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &noncanon); // Set terminal to noncanonical mode to allow for arrow key navigation

	// Sets up a signal handler for SIGCHLD, done because we do not want to block the parent process while waiting for background processes to finish
	struct sigaction sigac;  // Struct to handle signals
	sigac.sa_handler = sigchld; // When SIGCHLD signal is received, Set signal handler to handle_child
	sigemptyset(&sigac.sa_mask); // Clears signal set, making it so while handle_child is running, signals can still be received
	sigac.sa_flags = SA_NOCLDSTOP | SA_RESTART; // The SIGCHLD signal will only be sent when a child process terminates, and interrupted system calls are automatically restarted
	sigaction(SIGCHLD, &sigac, NULL); // Set action for SIGCHLD signal, which is to call handle_child and clean up background processes

	// Program loop
	while (should_run) {
		// Print the terminal prompt
		int len_prompt = MAX_DIR + 6; // Length of prompt, 6 is added to account for the 6 extra prompt chars "osh:> " (excluding directory name), allowing for 1 byte of chars
		char prompt[len_prompt]; // Array to store prompt
		print_prompt(prompt, len_prompt);

		// Read user input
		char input[MAX_LINE]; // Array to store user input
		noncanon_input(prompt, input); // Read user input char-by-char, store in input array
	   
		// Pre-parse checks and actions
		if (strlen(input) == 0) continue; // If nothing was inputted prior to the newline terminate char, run the program loop again
		if (input_exit(input, &should_run)) break;
		if (input_prev(input)) continue;
		if (input_history(input)) continue;
		add_history(input);

		// Parse input
		int bkgd = 0; // Flag to indicate if the process should run in the background
		char* inpRedirFile = NULL; // File to redirect input to
		char* outpRedirFile = NULL; // File to redirect output to
		int pipe_index = -1; // Index of pipe in args
		input_parse(input, args, &bkgd, &inpRedirFile, &outpRedirFile, &pipe_index); // Parse input into args array

		// Check if input was "cd" command
		if (input_cd(args)) continue;

		// Execute commands
		if (pipe_index == -1) { // If cmd doesn't include a pipe
			pid_t pid = fork(); // Fork a child process
			if (pid == 0) { // If child process
				if (inpRedirFile) { // If input redirection file is given, redirect input to it only in child process
					int fd = open(inpRedirFile, O_RDONLY); // Open file for read-only
					dup2(fd, STDIN_FILENO); // Redirect standard input to that file
					close(fd); // Close file descriptor
				}
				if (outpRedirFile) { // If output redirection file is given, redirect output to it only in child process
					int fd = open(outpRedirFile, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open file for write-only
					dup2(fd, STDOUT_FILENO); // Redirect standard output to that file
					close(fd); // Close file descriptor
				}
				if (execvp(args[0], args) == -1) { // Execute the command, if it returns -1, print error
					printf("Error: \"%s\" command not found\n", args[0]);
					exit(1); // Exit child process
				}
			} else if (pid > 0) {
				if (!bkgd) // If not a background process, wait for the child to finish
					wait(NULL); // Wait for child process to finish
			}
		} else { // If cmd includes a pipe
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &canon); // Set terminal back to canonical mode so piping features like "less" work
			int pipefd[2]; // Array to store pipe file descriptors
			pipe(pipefd); // Create a pipe
			pid_t pidL = fork(); // Fork a child process for left side of the pipe
			if (pidL == 0) { // If child process
				close(pipefd[0]); // Close read end of pipe
				dup2(pipefd[1], STDOUT_FILENO); // Redirect standard output to write end of pipe
				close(pipefd[1]); // Close write end of pipe
				execvp(args[0], args); // Execute the first command in the pipe
			}
			pid_t pidR = fork(); // Fork another child process for right side of the pipe
			if (pidR == 0) { // If child process
				close(pipefd[1]); // Close write end of pipe
				dup2(pipefd[0], STDIN_FILENO); // Redirect standard input to read end of pipe
				close(pipefd[0]); // Close read end of pipe
				execvp(args[pipe_index + 1], &args[pipe_index + 1]); // Execute the second command in the pipe
			}
			else if (pidR > 0) {
				close(pipefd[0]); // Close read end of pipe in parent process
				close(pipefd[1]); // Close write end of pipe in parent process
				waitpid(pidL, NULL, 0); // Wait for first child process to finish
				waitpid(pidR, NULL, 0); // Wait for second child process to finish
				tcsetattr(STDIN_FILENO, TCSAFLUSH, &noncanon); // Set terminal back to non-canonical mode for arrow key navigation  
			}
		}
	}

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &canon); // Reset terminal to canonical mode before ending program for safety
	printf("Exited successfully!\n");
	return 0;
}
