#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> // for input redirection
#include <sys/stat.h> //for input redirection
#include <dirent.h> //for autocomplete directory operations
#include <ctype.h> //to use isspace function
#include "../graph/tree_graph.c" //to create the graph pngs
const char *sysname = "dash";

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");

	for (i = 0; i < 3; i++) {
		printf("\t\t%d: %s\n", i,
			   command->redirects[i] ? command->redirects[i] : "N/A");
	}

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	for (int i = 0; i < 3; ++i) {
		if (command->redirects[i])
			free(command->redirects[i]);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)malloc(1);
		command->name[0] = 0;
	} else {
		command->name = (char *)malloc(strlen(pch) + 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<') {
			redirect_index = 0;
		}

		if (arg[0] == '>') {
			if (len > 1 && arg[1] == '>') {
				redirect_index = 2;
				arg++;
				len--;
			} else {
				redirect_index = 1;
			}
		}

		if (redirect_index != -1) {
			//this is the case when the operator next to file such as <out.txt
			if (strlen(arg)>1){
				command->redirects[redirect_index] = strdup(arg+1);
			}
			// if there is a space between them << out.txt
			else{
				pch = strtok(NULL,splitters); 
				if (pch){
					command->redirects[redirect_index] =strdup(pch);
				}
				else{
					printf("there is no file name after redirection\n");
				}
			}
			continue;
		}

		// normal arguments
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace() {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}

//PART 3 AUTOCOMPLETE
//To check a string is empty or it has only spaces we will define a helper function

int is_empty(const char *str){
	while(*str){
		if(!isspace((unsigned char) *str)){
			return 0;
		}
		str++;
	}
	return 1;
}

//Autocomplete function 
int autocomplete(char *input, size_t *index){
	char *last_word = strrchr(input,' '); //find the last space in given input
	if(last_word == NULL){
		last_word = input; //if there is no space then we need to autocomplete whole input
		}
	else{
		last_word ++; //go to the next word after space;
	}

	//determine the command (we will use it to separate cd from other commands so that we can list directories)
	char command[1024] = {0};
	sscanf(input,"%s",command);

	DIR *dir;
	struct dirent *entry;
	char matches[1024][1024]; //we have an array to store the matches
	int match_count = 0; //we will use this to keep count how many possible match
	
	//search current directory
	dir = opendir(".");
	if(dir !=NULL){
		while((entry = readdir(dir))!=NULL){
			//checking if current entry is a match or the input is empty
			if(is_empty(last_word) || strncmp(entry->d_name,last_word,strlen(last_word))==0){
				if(entry->d_name[0] != '.') { //we put this to ignore system hidden files
					if((strcmp(command, "cd")==0 && entry->d_type == DT_DIR) ||
						(strcmp(command,"cd")!=0 && entry->d_type == DT_REG)){
						//we store the match in the array
						strcpy(matches[match_count],entry->d_name);
						if(entry->d_type == DT_DIR){
							strcat(matches[match_count], "/");
						}
						match_count++;
					}
				}
			}
		}
		closedir(dir);
	}

	//now we need to check the matches
	if(match_count ==1){
		//only one match we need to complete the input
		strcpy(last_word, matches[0]);
		*index = strlen(input); //we use index to update the pointer to the end of input
		printf("\n");
		show_prompt(); 
		printf("%s",input); //display the autocompleted input
		fflush(stdout);
	}
	else if(match_count > 1){
		//multiple matches show the matches and redisplay the prompt
		printf("\n");
		for(int i=0;i<match_count;i++){
			printf("%s ", matches[i]);
		}
		printf("\n");
		show_prompt();
		printf("%s",input);
		fflush(stdout);

	}
	else if(match_count==0 && is_empty(last_word)){
		//if there is only command and no match
		printf("\n");
		dir = opendir(".");
		if(dir !=NULL){
			while((entry = readdir(dir))!=NULL){
				if(entry->d_name[0] != '.') {
					if((strcmp(command,"cd")==0 && entry->d_type == DT_DIR) ||
							(strcmp(command,"cd") !=0 && entry->d_type == DT_REG)){
						printf("%s ", entry->d_name);
					}
				}
			}
			closedir(dir);
		}
		printf("\n");
		show_prompt();
		printf("%s",input);
		fflush(stdout);
	}
	return match_count;
}


/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	show_prompt();
	buf[0] = 0;

	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		// handle tab
		if (c == 9) {
			buf[index++] = '\0'; // autocomplete
			if(autocomplete(buf,&index)>0){
				index=strlen(buf);
			}
			continue;
		}

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}

	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main() {
	while (1) {
		struct command_t *command = malloc(sizeof(struct command_t));

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
		if (code == EXIT) {
			break;
		}

		free_command(command);
	}

	printf("\n");
	return 0;
}

// PART 1 
int resolve_command_path(const char *command_name, char *resolved_path) {
    char *paths = getenv("PATH"); // Get the system PATH
    char *path = strtok(paths, ":"); //Tokenize to get the paths
    while (path != NULL) {
        sprintf(resolved_path, "%s/%s", path, command_name); //It gets the path
        if (access(resolved_path, X_OK) == 0) {
            return 1; // Path found
        }
        path = strtok(NULL, ":");
    }
    return 0; // Command not found
}

//PART 3 KUHEX IMPLEMENTATION

void kuhex(const char *filename, int group_size){
	FILE *file = fopen(filename, "rb");
	if (!file){
		perror("Error while opening the file");
		return;
	}

	unsigned char buffer[16]; //this is our buffer to hold the 16 bytes
	size_t bytes_read;
	unsigned long offset = 0;

	while((bytes_read = fread(buffer,1,16,file))>0){
		//we print the beginning line that is presented at project pdf
		printf("%08lx: ", offset);
		offset += bytes_read;
		size_t hex_chars_printed = 0; // we will use this to fix alignment

		//Now we print the hex part according to the given grouping (it can be 1,2,4,8,16)
		for(size_t i=0; i<16;i++){
			// we print the hex of each group
			if(i<bytes_read){
				printf("%02x",buffer[i]);
				hex_chars_printed += 2;
				if((i+1) %group_size ==0 && (i+1) <16) { //this is the check to separate the groups
					printf(" ");
					hex_chars_printed +=1;
				}
			}else{
				printf("  "); //we put this if a line is not full and there are missigng bytes
				hex_chars_printed +=2;
				if((i+1)%group_size ==0 && (i+1) <16){
					printf(" ");
					hex_chars_printed+=1;
				}
			}
		}

		//alignment for ASCII part
		size_t spaces_needed = 50 - hex_chars_printed;
		for(size_t i=0; i<spaces_needed;i++){
			printf(" ");
		}

		//now we need to write the text at the end
		for(size_t i=0; i<bytes_read;i++){
			if(buffer[i]>=32 && buffer[i] <=126){
				printf("%c",buffer[i]); //we write the char according to its ASCII code
			}
			else{
				printf(" "); //this is for the spaces 
			}

		}
		printf("\n");
	}
	fclose(file);
}

//PART 4 PSVIS IMPLEMENTATION
//

//We will create functions so that when psvis is called the kernel will be loaded automatically
void load_kernel_module(int pid){
	//it checks if the module is already loaded
	if(system("lsmod | grep mymodule > /dev/null") !=0){
		char command[512];
		snprintf(command,sizeof(command), "sudo insmod module/mymodule.ko pid=%d",pid);
		if(system(command)!=0){
			fprintf(stderr, "There is an error while loading the kernel module.\n");
			exit(1);
		}
		printf("psvis kernel module loaded with PID: %d\n",pid);
	}
	else{
		printf("psvis kernel module already loaded\n");
	}
}

//method to unload the model after we use the function
void unload_kernel_module(){
	if(system("lsmod | grep mymodule > /dev/null") ==0){
		if(system("sudo rmmod mymodule") !=0){
			fprintf(stderr,"There is an error while unloading the kernel module\n");
		}
		else{
			printf("psvis kernel module unloaded\n");
		}
	}
}

//now we need the function to read process tree from /proc/psvis

void read_process_tree(){
	FILE *file = fopen("/proc/psvis","r");
	if(!file){
		perror("Error while opening /proc/psvis");
		return;
	}

	char line[256];
	printf("Process Tree:\n");
	while(fgets(line, sizeof(line), file)){
		printf("%s",line); //printing the lines of process tree one by one
	}
	fclose(file);
 }

//We will use this function to convert the given png file to txt and dot files
void replace_extension(const char *input, char *output, const char *ext){
	char *dot_pos = strrchr(input,'.'); // find the final dot in given file
	if(dot_pos){
		size_t len = dot_pos - input; //name length of the file
		strncpy(output,input,len); //copy only the name of the file
		output[len] = '\0'; //null terminate
		}
	else {
		strcpy(output,input); //if the file extension is not given copy whole
		}
	strcat(output,ext); //add the extension

}

//now we need to define the psvis_command that we will use in process_command function.
void psvis(int pid,const char *output_file){
	load_kernel_module(pid); //it loads the kernel module with the given PID
	
	const char *ext = strrchr(output_file,'.');
	if(!ext){
		fprintf(stderr,"ERROR:supported extensions are png,jpg and jpeg.\n");
		unload_kernel_module();//unload the kernel module
		return;
	}
	ext++;
	if(strcmp(ext,"png")!=0 && strcmp(ext,"jpg")!=0 && strcmp(ext,"jpeg")!=0){
		fprintf(stderr,"ERROR:supported extensions are png, jpg and jpeg.\n");
		unload_kernel_module();
		return;
	}

	read_process_tree(); //this is used to read the created process tree from proc
	
	printf("\nDo you want to draw the process tree graph? (y/n): ");
	char choice;
	scanf(" %c",&choice);
	choice=tolower(choice);
	if(choice=='y'){

	//we need to save the process tree to a file
	char txt_file[256];
	replace_extension(output_file,txt_file,".txt");
	FILE *file =fopen(txt_file,"w");
	if(!file){
		perror("ERROR opening file for process tree");
		unload_kernel_module();
		return;
	}

	FILE *proc_file = fopen("/proc/psvis","r");
	if(!proc_file){
		perror("ERROR reading while /proc/psvis\n");
		fclose(file);
		unload_kernel_module();
		return;
	}

	char line[256];
	while(fgets(line,sizeof(line),proc_file)){
		fprintf(file,"%s",line);
	}
	fclose(proc_file);
	fclose(file);
	printf("Process tree saved to %s\n",txt_file);

	//generate the dot file to create the graph
	char dot_file[256];
	replace_extension(output_file,dot_file,".dot");
	if(generate_dot_file(txt_file,dot_file)==0){
		printf("Dot file is generated as %s\n",dot_file);
	}
	else{
		fprintf(stderr,"ERROR while generating the dot file\n");
		unload_kernel_module();
		return;
	}

	//now we can generate the png file
	if(generate_image_file(dot_file,output_file)==0){
		printf("Process tree graph is saved as %s\n",output_file);
	}
	else{
		fprintf(stderr,"ERROR while generating png file\n");
		unload_kernel_module();
		return;
	}
	}
	else{
		printf("Graph generation skipped.\n");
	}

	unload_kernel_module(); //then it automatically unloads it after the execution done
}


int process_command(struct command_t *command) {
	int r;

	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0) {
		if (command->arg_count > 0) {
			r = chdir(command->args[1]);
			if (r == -1) {
				printf("-%s: %s: %s\n", sysname, command->name,
					   strerror(errno));
			}

			return SUCCESS;
		}
	}
	//PART 3 ADDING KUHEX METHOD TO PROCESS COMMAND FUNCTION
	if(strcmp(command->name,"kuhex")==0){
		if(command->arg_count <2){ //if there are few arguments we need to provide the correct input format
			printf("Usage: kuhex -g group_siz <file<\n");
			return SUCCESS;
		}
		int group_size =1; //default
		char *filename;

		//parsing the arguments
		if(strcmp(command->args[1], "-g")==0){
			if (command->arg_count<4){
				printf("Usage: kuhex -g group_size <file>\n");
				return SUCCESS;
			}
			group_size = atoi(command->args[2]);
			filename = command->args[3];
		}
		else{
			filename = command->args[1]; //default usage without -g
		}
		
		//we check the group size if it is 1,2,4,8,16
		if(group_size !=1 && group_size !=2 && group_size !=4 && group_size != 8 && group_size != 16){
			printf("Invalid group size. This function support 1,2,4,8,16 sizes");
			return SUCCESS;
		}

		//now we should use the function we defined
		kuhex(filename,group_size);
		return SUCCESS;
	}

	//PART 4 PSVIS 
	if(strcmp(command->name,"psvis")==0){
		if(command->arg_count <4){ //it should be written according to the rules we determine
			printf("psvis command usage: psvis <PID><output_file>\n");
			return UNKNOWN;
		}
		int pid = atoi(command->args[1]); //we get the given argument PID and convert it to int
		if(pid <=0){
			printf("PID must be greater or equal than 1\n");
			return SUCCESS;
		}
		const char *output_file = command->args[2]; //added this new to put the png to the given file
		psvis(pid,output_file); // if the arguments are given correctly, we call the psvis command
		return SUCCESS;
	}
			
		
	//PART 2 PIPING AND REDIRECTION (updated for multiple piping)
	//We will be adding the piping conditions here before the commands without piping
	struct command_t *current = command;
	int prev_pipe_fd[2] = {-1,1}; //descriptors for the previous command
	while(current!=NULL){
		int pipe_fd[2] = {-1,1};

		// we check if there is a new command and create a pipe for it
		if (current->next !=NULL){
			if(pipe(pipe_fd)==-1){
				perror("pipe");
				return UNKNOWN;
			}
		}

		pid_t pid =fork();

		if(pid==0){ //for child process
			//we need to check input redirection
			if(current->redirects[0]){
				int fd = open(current->redirects[0], O_RDONLY);
				if(fd==-1){
					perror("Error opening input file");
					exit(1);
				}
				dup2(fd,STDIN_FILENO);
				close(fd);
			}
			//output redirection for last command
			if(!current->next && current->redirects[1]){
				int fd = open(current->redirects[1],O_WRONLY | O_CREAT | O_TRUNC,0644);
				if(fd==-1){
					perror("Error opening output file");
					exit(1);
				}
				dup2(fd,STDOUT_FILENO);
				close(fd);
			}

			//append redirection
			if(!current->next && current->redirects[2]){
				int fd = open(current->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
				if(fd==-1){
					perror("Error opening append file");
					exit(1);
				}
				dup2(fd,STDOUT_FILENO);
				close(fd);
			}

			//redirection from the previous pipe
			if(prev_pipe_fd[0]!=-1){
				dup2(prev_pipe_fd[0],STDIN_FILENO);
				close(prev_pipe_fd[0]);
				close(prev_pipe_fd[1]);
			}
			//redirect output to the current pipe:
			if(current->next!=NULL){
				close(pipe_fd[0]); //close unused read end
				dup2(pipe_fd[1],STDOUT_FILENO);
				close(pipe_fd[1]); //close write end 
			}


		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// TODO: do your own exec with path resolving using execv()

			//execution of the command 
			char resolved_path[1024];
			if(resolve_command_path(current->name,resolved_path)){
				execv(resolved_path,current->args);
				perror("execv");
			}
			else{
				printf("dash: command not found: %s\n",current->name);
			}
			exit(1);
		}
		else if(pid > 0){ // check the parent process
				//close the previous pipes
				if(prev_pipe_fd[0]!=-1){
					close(prev_pipe_fd[0]);
					close(prev_pipe_fd[1]);
				}
				//update the pipe
				prev_pipe_fd[0] = pipe_fd[0];
				prev_pipe_fd[1]= pipe_fd[1];

				// TODO: implement background processes here
				// wait(0); //wait for child process to finish
				if(!command->background){
					int status;
					waitpid(pid,&status,0);
				}
				else{
					printf("Process is running in background [PID: %d]\n",pid);
				}
		}
		else{
			perror("fork");
			return UNKNOWN;
		}
		//move to the next command in pipeline by changing the current command
		current = current->next;
	}

	//we should also check if there is any remaining open pipe
	if(prev_pipe_fd[0]!=-1){
		close(prev_pipe_fd[0]);
		close(prev_pipe_fd[1]);
	}
	return SUCCESS;
}

	
