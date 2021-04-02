#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>

/*-------------------------------------------*/
// For Part 1
#define WHICH_DELIMITER   ":"
// For Part 3
#define RED   "\x1B[31m"
#define GREEN   "\x1B[32m"
#define BLUE   "\x1B[34m"
#define RESET "\x1B[0m"

#define BUFFER_SIZE 9999
#define READ_END	0
#define WRITE_END	1
/*-------------------------------------------*/

const char * sysname = "seashell";

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
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
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
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
		if (len>0 && buf[len-1]=='&') // background
			command->background=true;

			char *pch = strtok(buf, splitters);
			command->name=(char *)malloc(strlen(pch)+1);
			if (pch==NULL)
				command->name[0]=0;
				else
					strcpy(command->name, pch);

			command->args=(char **)malloc(sizeof(char *));

			int redirect_index;
			int arg_index=0;
			char temp_buf[1024], *arg;
			while (1)
			{
				// tokenize input on splitters
				pch = strtok(NULL, splitters);
				if (!pch) break;
				arg=temp_buf;
				strcpy(arg, pch);
				len=strlen(arg);

				if (len==0) continue; // empty arg, go for next
				while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
				{
					arg++;
					len--;
				}
				while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
				if (len==0) continue; // empty arg, go for next

				// piping to another command
				if (strcmp(arg, "|")==0)
				{
					struct command_t *c=malloc(sizeof(struct command_t));
					int l=strlen(pch);
					pch[l]=splitters[0]; // restore strtok termination
					index=1;
					while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

					parse_command(pch+index, c);
					pch[l]=0; // put back strtok termination
					command->next=c;
					continue;
				}

				// background process
				if (strcmp(arg, "&")==0)
					continue; // handled before

				// handle input redirection
				redirect_index=-1;
				if (arg[0]=='<')
					redirect_index=0;
				if (arg[0]=='>')
				{
					if (len>1 && arg[1]=='>')
					{
						redirect_index=2;
						arg++;
						len--;
					}
					else redirect_index=1;
				}
				if (redirect_index != -1)
				{
					command->redirects[redirect_index]=malloc(len);
					strcpy(command->redirects[redirect_index], arg+1);
					continue;
				}

				// normal arguments
				if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
						|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
				{
					arg[--len]=0;
					arg++;
				}
				command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
				command->args[arg_index]=(char *)malloc(len+1);
				strcpy(command->args[arg_index++], arg);
			}
			command->arg_count=arg_index;
			return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
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
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


	//FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
	while (1)
	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c==27 && multicode_state==0) // handle multi-code keys
		{
			multicode_state=1;
			continue;
		}
		if (c==91 && multicode_state==1)
		{
			multicode_state=2;
			continue;
		}
		if (c==65 && multicode_state==2) // up arrow
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
	}
	if (index>0 && buf[index-1]=='\n') // trim newline from the end
		index--;
	buf[index++]=0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}
int process_command(struct command_t *command);
int main()
{
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "")==0) return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}
    /*-----------------------------------------------------------------------------------------------------------------------------------------------------*/
	char read_msg[BUFFER_SIZE];
	char write_msg[BUFFER_SIZE];
	int fd[2];
	if (pipe(fd) == -1) {
		fprintf(stderr,"Pipe failed");
		return 1;
	}
    /*----------------------------------------------------------------------------------------------------------------------------------------------------*/
	pid_t pid=fork();
	if (pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// increase args size by 2
		command->args=(char **)realloc(
				command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];

			// set args[0] as a copy of name
			command->args[0]=strdup(command->name);
			// set args[arg_count-1] (last) to NULL
			command->args[command->arg_count-1]=NULL;

			//execvp(command->name, command->args); // exec+args+path

			/*---------------------------------------------------------------------------------------------------------------------------------------------*/		
			// Part 2
			if(strcmp(command->name,"shortdir")==0){	  
				if(command->args[1] == NULL){
					printf("Missing parameters\n");
					return SUCCESS;
				} 	    		    
				char *comm = command->args[1];
				char *filePath = "/home/mertcan/Desktop/shortdir.txt";
				char *tempfilePath = "/home/mertcan/Desktop/tempshortdir.txt";

				if( strcmp(comm,"set") == 0){   // shortdir set - command
					if(command->args[2] == NULL){
						printf("Please enter an alias name\n");
					}
					char *name = command->args[2];
					char hold[1024];    // Temp string to hold the line information
					strcpy(hold,name);
					strcat(hold,":");
					char cwd[1024];     // Location information
					getcwd(cwd, sizeof(cwd));
					char currPath[1024];
					strcpy(currPath, cwd);
					strcat(hold,cwd);
					FILE *fptr;
					fptr = fopen(filePath,"a+");    // Need both append and reading modes
					char buffer[99999];
					char *last_token;

					while( fgets(buffer, 99999, fptr) != NULL ){  
						last_token = strtok( buffer, ":" );
						if(strcmp(last_token, name)==0){    // If a given name is already an existing association
							fclose(fptr);   // To do not mess up with open files, close it and reopen it
							fptr = fopen(filePath,"a+");
							FILE *ftemp;    // Temporary file to hold the original file with deleted line
							ftemp = fopen(tempfilePath,"a");
							char buffer2[99999];
							char *last_token2;
							while( fgets(buffer2, 99999, fptr) != NULL ){  
								last_token2 = strtok( buffer2, ":" );
								if(strcmp(last_token2,name)!=0){    // Copy all the lines except the one with given name
									char line[200];
									strcpy(line, last_token2);
									last_token2 = strtok( NULL, ":" );
									strcat(line, ":");
									strcat(line, last_token2);
									fprintf(ftemp,"%s",line);
								}   
							}
							fclose(ftemp);  // Close both files
							fclose(fptr);
							rename(tempfilePath,filePath);  // Change the name of temporary file to original file name
							fptr = fopen(filePath,"a"); // Reopen the file
						}                
					}

					fprintf(fptr,"%s\n",hold);  // Write to file
					printf("%s is set as an alias for %s\n", name, currPath);   // Print to console
					fclose(fptr);

				} else if(strcmp(comm,"del")==0){
					if(command->args[2] == NULL){
						printf("Please enter an alias name\n");
					}
					char *name = command->args[2];
					FILE *fptr;
					fptr = fopen(filePath,"r"); // Open in read only
					FILE *ftemp;
					ftemp = fopen(tempfilePath,"a");

					char buffer[99999];
					char *last_token;
					while( fgets(buffer, 99999, fptr) != NULL ){  
						last_token = strtok( buffer, ":" );
						if(strcmp(last_token,name)!=0){     // Copy all the lines except the one with given name
							char line[200];
							strcpy(line, last_token);
							last_token = strtok( NULL, ":" );
							strcat(line,":");
							strcat(line, last_token);
							fprintf(ftemp,"%s",line);
						}   
					}
					fclose(ftemp);
					fclose(fptr);
					rename(tempfilePath,filePath);
				} else if(strcmp(comm,"clear")==0){     
					FILE *fptr;
					fptr = fopen(filePath,"w");     // Opening a file in writing mode removes all entries in the file,
					fclose(fptr);                   // which is enough for our purpose

				} else if(strcmp(comm,"list")==0){
					FILE *fptr;
					fptr = fopen(filePath,"r");
					char buffer[99999];
					while( fgets(buffer, 99999, fptr) != NULL ){  
						printf("%s", buffer);               // Prints all the lines  
					}
					fclose(fptr);

				} else if(strcmp(comm,"jump")==0){    
					if(command->args[2] == NULL){
						printf("Please enter an alias name\n");
					}
					char *name = command->args[2];
					FILE *fptr;
					fptr = fopen(filePath,"r");     // Open in read mode
					char buffer[99999];
					char *last_token;
					char line[256];
					while( fgets(buffer, 99999, fptr) != NULL ){  
						last_token = strtok( buffer, ":" );
						if(strcmp(last_token,name)==0){     // Find the line with given name
							last_token = strtok( NULL, ":" );   
							strcpy(line, last_token);
						}                          
					}
					fclose(fptr);
					const char *path = strtok(line , "\n");     // Remove the \n at the end of the line

                    //chdir(path);       // Does not change directory. So we switched to the pipes

					close(fd[READ_END]);		
					write(fd[WRITE_END], path, strlen(path)+1);     // Send the path information to the parent through a pipe
					close(fd[WRITE_END]);
	
				} else {
					printf("Invalid argument\n");
				}

			}
			// Part 3
			else if(strcmp(command->name,"highlight")==0){		
				if(command->args[3] == NULL || command->args[2] == NULL || command->args[1] == NULL) { // Missing parameters
					printf("Missing parameters\n");
					return SUCCESS;
				}
				char *word = command->args[1];
				char *file = command->args[3];
				char *color = command->args[2];
				if(strcmp(color, "r") != 0 && strcmp(color, "g") != 0 && strcmp(color, "b") != 0){ // Invalid color
					printf("Invalid color\n");
					return SUCCESS;
				}

				FILE *fptr;
				fptr = fopen(file,"r");     // Open the file in read only mode
				char buffer[99999];
				char *last_token;
				char delim[] = {" ,.:;\t\r\n\v\f\0"};       // Delimiters to tokenize the text file
				while( fgets(buffer, 99999, fptr) != NULL ){  
					char currentLine[1024];
					strcpy(currentLine, buffer);
					int flag = 0;   // Flag to show, whether given word is included in that line
					last_token = strtok( buffer, delim );
					while( last_token != NULL ){
						if(strcasecmp(last_token,word)==0){
							flag = 1;
						}
						last_token = strtok( NULL, delim );
					}
					if(flag == 1){  // If given word is included in that line
						char *inLineToken = strtok(currentLine, delim);
						while(inLineToken){
							if( strcasecmp(inLineToken, word) ==0){     // Case-insensitive comparing
								if( strcmp(color, "r") == 0){
									printf(RED "%s ", inLineToken);
									printf(RESET);
								} else if (strcmp(color, "g") == 0){
									printf(GREEN "%s ", inLineToken);
									printf(RESET);
								} else if ( strcmp(color, "b") == 0){
									printf(BLUE "%s ", inLineToken);
									printf(RESET);
								}   
							} else {
								printf("%s ", inLineToken);
							}
							inLineToken = strtok(NULL,delim);
						}
						printf(".\n");
					}                        
				}

			}
			// Part 4
			else if(strcmp(command->name,"goodMorning")==0){
				if(command->args[2] == NULL|| command->args[1] == NULL) { // Missing parameters
					printf("Missing parameters\n");
					return SUCCESS;
				}
				char alarmfilePath[1024];     // Location information
				getcwd(alarmfilePath, sizeof(alarmfilePath)); 
				strcat(alarmfilePath, "/alarm.txt");    // Create a file called "alarm.txt" at the current location
				char *time = command->args[1];
				char *musicFile = command->args[2];
				FILE *falarm;
				falarm = fopen(alarmfilePath,"w");

				char s[256];
				strcpy(s, time);
				char * hour = strtok(s, ".");   // Tokenize the time entered by user
				char * minute = strtok(NULL, ".");
				fprintf(falarm, "%s %s * * * XDG_RUNTIME_DIR=/run/user/$(id -u) /usr/bin/rhythmbox-client --play %s\n", minute, hour, musicFile);
				fclose(falarm); 
				execlp("crontab","crontab", alarmfilePath, NULL);
			}

			//Part 5
			else if(strcmp(command->name, "kdiff") == 0){
				char *flag = command->args[1];
				char file1[100];
				char file2[100];

				if(flag == NULL) {
					printf("Please enter at least 2 file names\n");
				} else if(command->args[2] == NULL){
					printf("Please enter a valid number of arguments\n");
				} else {
					if(command->args[3] == NULL){   
						strcpy(file1, command->args[1]);
						strcpy(file2, command->args[2]);
						strcpy(flag, "-a");
					} else {
						strcpy(file1,command->args[2]);
						strcpy(file2,command->args[3]);
					}   

					if( strcmp(flag, "-a") ==0) {   // Compare line by line
						FILE *fPtr1;
						FILE *fPtr2;
						fPtr1 = fopen(file1,"r");
						fPtr2 = fopen(file2,"r");
						char lines1[1000][1000];    // 2D array to hold the lines of file 1
						char lines2[1000][1000];    // 2D array to hold the lines of file 2
						int number = 0;
						int totalMistakes = 0;

						char buffer1[99999];
						while( fgets(buffer1, 99999, fPtr1) != NULL ){  // Populate array 1
							strcpy(lines1[number],buffer1);
							number++;                  
						}
						fclose(fPtr1);

						number = 0;
						char buffer2[99999];
						while( fgets(buffer2, 99999, fPtr2) != NULL ){  // Populate array 2
							strcpy(lines2[number],buffer2);
							number++;                  
						}
						fclose(fPtr2);

						for(int i =0; i<number; i++){   // Compare 2 array, line by line
							if(strcmp( lines1[i], lines2[i]) != 0){
								totalMistakes++;
								printf("%s:Line %d: %s", file1, i+1, lines1[i]);
								printf("%s:Line %d: %s", file2, i+1, lines2[i]);
							}
						}

						if(totalMistakes == 0){
							printf("%s","The two files are identical\n");
						}else{
							printf("%d different lines found\n",totalMistakes);
						}

					} else if( strcmp(flag, "-b") ==0) {    // Compare byte by byte
						FILE *fPtr1;
						FILE *fPtr2;
						fPtr1 = fopen(file1,"rb");  // Open in rad byte mode
						fPtr2 = fopen(file2,"rb");

						unsigned long pos;
						int c1, c2;
						int totalMistakes= 0;
						for (pos = 0;; pos++) {     // Read a byte from both files and compare
							c1 = getc(fPtr1);
							c2 = getc(fPtr2);
							if (c1 != c2){
								totalMistakes += 1;
							}
							if (c1 == EOF || c1 == EOF)
								break;
						}
						if (totalMistakes == 0) {
							printf("The two files are identical and have %lu bytes\n", pos);
						} else{
							printf("The two files are different in %d bytes\n", totalMistakes);
						} 
					} else{
						printf("Given mode argument is invalid. Please use -a or -b.\n");
					}
				}



			}// Part 6
			else if(strcmp(command->name,"zoom")==0){
				// -s save -o open -d delete -l list -c clear
				char *mode = command->args[1];
				char *class_name = command->args[2];

				char *fileName = "zoom_classes.txt";
				char *tempfileName = "tempzoom_classes.txt";

				FILE *fptr;
				fptr = fopen(fileName,"a+");    // Need both append and reading modes

				if( strcmp(mode, "-s") ==0) {   //save a class
					char *link = command->args[3];
					char *password = command->args[4];	
					char *saved_class[99999];
					strcpy(saved_class,class_name);
					strcat(saved_class," ");
					strcat(saved_class,link);
					strcat(saved_class," ");
					strcat(saved_class,password); 

					char buffer[99999];
					char *last_token;
					while( fgets(buffer, 99999, fptr) != NULL ){  
						last_token = strtok( buffer, " " );
						if(strcmp(last_token, class_name)==0){    // If a given name is already an existing association
							fclose(fptr);   // To do not mess up with open files, close it and reopen it
							fptr = fopen(fileName,"a+");
							FILE *ftemp;    // Temporary file to hold the original file with deleted line
							ftemp = fopen(tempfileName,"a");
							char buffer2[99999];
							char *last_token2;
							while( fgets(buffer2, 99999, fptr) != NULL ){  
								last_token2 = strtok( buffer2, " " );
								if(strcmp(last_token2,class_name)!=0){    // Copy all the lines except the one with given name
									char line[512];
									strcpy(line, last_token2);
									last_token2 = strtok( NULL, " " );
									strcat(line, " ");
									strcat(line, last_token2);
									last_token2 = strtok( NULL, " " );
									strcat(line, " ");
									strcat(line, last_token2);
									fprintf(ftemp,"%s",line);
								}   
							}
							fclose(ftemp);  // Close both files
							fclose(fptr);
							rename(tempfileName,fileName);  // Change the name of temporary file to original file name
							fptr = fopen(fileName,"a"); // Reopen the file
						}                
					}

					fprintf(fptr,"%s\n",saved_class);
					fclose(fptr);

				} else if( strcmp(mode, "-o") ==0) {
					char buffer[99999]; 
					char *last_token;
					while( fgets(buffer, 99999, fptr) != NULL ){  
						last_token = strtok( buffer, " " );

						if(strcmp(last_token,class_name)==0){    
							char *link =  strtok( NULL, " " );    
							char *password  = strtok( NULL, " " );
							printf("Password for the class is: %s\n",password);       // To ease of use, print the password to the console      
							char *xdg[99999];
							strcat(xdg,"xdg-open ");
							strcat(xdg,link);
							system(xdg);        // Use system() call to open the link in the browser
							fclose(fptr);
						}   
					} 

				} else if( strcmp(mode, "-d") == 0) { 
					FILE *ftemp;
					ftemp = fopen(tempfileName,"a");

					char buffer[99999];
					char *last_token;
					while( fgets(buffer, 99999, fptr) != NULL ){  
						last_token = strtok( buffer, " " );
						if(strcmp(last_token,class_name)!=0){     // Copy all the lines except the one with given name
							char line[512];
							strcpy(line, last_token);
							last_token = strtok( NULL, " " );
							strcat(line," ");
							strcat(line, last_token);
							last_token = strtok( NULL, " " );
							strcat(line," ");
							strcat(line, last_token);
							fprintf(ftemp,"%s",line);
						}   
					}
					fclose(ftemp);
					fclose(fptr);
					rename(tempfileName,fileName);   // Rename the temp file as the original one

				} else if( strcmp(mode, "-l") == 0){
					char buffer[99999];
					while( fgets(buffer, 99999, fptr) != NULL ){  
						printf("%s", buffer);               // Prints all the lines                      
					}
					fclose(fptr);

				} else if( strcmp(mode, "-c") == 0){
					fclose(fptr);
					fptr = fopen(fileName,"w");     // Opening a file in writing mode removes all entries in the file,
					fclose(fptr); 
				}


			}
			// Part 1
			else {
				char *path = strdup(getenv("PATH"));
				if (NULL == path) return NULL;
				char *tok = strtok(path, WHICH_DELIMITER);  // Tokenize environment paths with ":"
				char location[1024];

				while (tok) {
					// path
					int len = strlen(tok) + 2 + strlen(command->name);
					char *file = malloc(len);
					if (!file) {    // If there is not enough memory to open the corresponding file, free the 
						free(path); // path variable and return.
						return NULL;
					}

					sprintf(file, "%s/%s", tok, command->name);     // Desired format for wanted command

					if (0 == access(file, X_OK)) {  // If file is openable, in the current location
						free(path);
						strcpy(location,file);  // Copy the location information and pass it to execv in below
					}

					// next token
					tok = strtok(NULL, WHICH_DELIMITER);
					free(file);
				}

				execv(location, command->args);
			}	
			/*---------------------------------------------------------------------------------------------------------------------------------------------*/
			exit(0);
			/// TODO: do your own exec with path resolving using execv()
	}
	else
	{
		wait(0);
		close(fd[WRITE_END]);
		read(fd[READ_END], read_msg, BUFFER_SIZE);  // Get output path from the pipe, which comes from the jump call of shortdir command
		chdir(read_msg);        // Change directory to the path, derived from the pipe
		close(fd[READ_END]);

		if (!command->background){
			wait(0); // wait for child process to finish
		}
		return SUCCESS;
	}

	// TODO: your implementation here

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
