#include <ctype.h>
#include <stdio.h>
#include <stdbool.h> //true, false
#include <unistd.h>  // getcwd fork
#include <stdlib.h> //free malloc
#include <string.h> // strcmp, strtok
#include <sys/types.h>  // pid_t
#include <sys/wait.h> // waitpid
#include <fcntl.h> //open, O_RDONLY, O_WRONLY
#include <sys/stat.h> //S_IRUSR, S_IWUSR
#include <signal.h> // signal, kill, SIGCONT
#include <errno.h> // errno



int main() {

  //ignore these signals
  // Ctrl-C
  signal(SIGINT, SIG_IGN); 
  
  signal(SIGQUIT, SIG_IGN);  
  // Ctrl-Z
  signal(SIGTSTP, SIG_IGN); 
  // avoid being stopped when handing terminal foreground group
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  

  //format:
  // An opening bracket [.
  // The word nyush.
  // A whitespace.
  // The basename of the current working directory.
  // A closing bracket ].
  // A dollar sign $.
  // Another whitespace.
  

  /**
   * source code of getcwd():
   * char *getcwd(char *buf, size_t size);
   * 
   * char *buf: where it write the path to
   * size_t size: max size the method can write to, like a reminder
   * ......
   * return NULL when fail(only when exceed the size)
   */

  char * userlines=NULL;
  size_t maxline=0;
  int joblist[1000];
  //first[1000]is max joblist command num, second[1000] is largest char length for each
  char joblist_user_lines[1000][1000];
  memset(joblist, 0, sizeof(joblist));
  memset(joblist_user_lines, 0, sizeof(joblist_user_lines));
  int joblist_index=-1;
  while (true)
  {


    char directory [1000];

    //write directory into the directory memory
    getcwd(directory, sizeof(directory));

    //basename, extract thing after the last slash
    char* basename=NULL;
    int last_slash=0;
    for (int i = 0; directory[i] != '\0'; i++) 
    {
      if (directory[i] == '/') 
      {
        last_slash = i;
      }
    }
    //if is the root
    if (last_slash == 0 && directory[1] == '\0') 
    {
    basename = "/";
    }
    else{
    basename = &directory[last_slash + 1];
    }
    
    //read from basement[0] to \0
    printf("[nyush %s]$ ", basename);
    
    /**
     * for fflush:
     * make sure user can see the prompt, flush it out of buffer
     * Upon successful completion 0 is returned.  Otherwise, EOF is returned and errno is set to indicate the error.
     */
    fflush(stdout);
    
    /* getline(char **lineptr, size_t *n, FILE *stream)
    read a line into char*, malloc memory, restore size, return length or -1 when fail
    userline will be covered each loop time, just need to free it after while loop(exit)
    */
    int input_length =getline(&userlines,&maxline,stdin);
    //when stdin reach eof, break
    if (input_length == -1) {break; }
    char original_userline[1000];
    snprintf(original_userline, sizeof(original_userline), "%s", userlines);

    
    /**
     * Parsing the command：strtok_r()
     * breaks a string into a sequence of zero or more nonempty tokens
     *  char *strtok(char *str, const char *delim);
     */

    // for instance, for "ls -l /home\n"
    // parsing to: {"ls", "-l", "/home", NULL}

    char *args[1000]={NULL};
    int i = 0;

    char *split_into_arrays = strtok(userlines, " \n"); 
    while (split_into_arrays != NULL) {
        args[i++] = split_into_arrays;
        split_into_arrays = strtok(NULL, " \n");
    }
    args[i] = NULL;  // add NULL at the end

    // now args = {"ls", "-l", "/home", NULL}

    //need to have a handler method working here for command check
    /**
     * Need a Handler to decide if the command is valid:
     * only the first program may redirect its input (using <), and only the last program may redirect its output (using > or >>). 
     * If there is only one program in a command, it may redirect both input and output.
     
      In each command, there are no more than one input redirection and one output redirection.
      Built-in commands (e.g., cd) cannot be I/O redirected or piped.
      
     * any error in parsing the command, then your shell should print the following error message to STDERR and
      prompt for the next command.
      Error: invalid command\n
     */

    //part of command handler, avoid null user input, blank line as valid command
    if (args[0] == NULL) {continue; }

    //built-in command
      bool flag_cd=false;
      bool flag_jobs=false;
      bool flag_fg=false;
      // bool flag_exit=false;
      bool flag_pipes=false;

      int count_cd=0;
      int count_jobs=0;
      int count_fg=0;
      int count_exit=0;

      int count_pipes=0;
      int pipe_positions[100];

      for (int i = 0; args[i] != NULL; i++)  
      {
        if (strcmp(args[i],"cd")==0)
        {
          flag_cd=true;
          count_cd++;
        }
        if (strcmp(args[i],"exit")==0)
        {
          //flag_exit=true;
          count_exit++;
        }
        if (strcmp(args[i],"fg")==0)
        {
          flag_fg=true;
          count_fg++;
        }
        if (strcmp(args[i],"|")==0)
        {
          flag_pipes=true;
          pipe_positions[count_pipes] = i;
          count_pipes++;
        }
        if (strcmp(args[i],"jobs")==0)
        {
          flag_jobs=true;
          count_jobs++;
        }
      }

      if (count_cd>1||count_exit>1||count_fg>1||count_jobs>1){
        fprintf(stderr, "Error: invalid command\n");
        continue;
      }
    
    
    
     //redirection
      bool flag_output=false;
      bool flag_input=false;
      bool flag_double_output=false;
    
    
      int count_input=0;
      int count_output=0;
      int count_double_output=0;
      
      char * store_file_input = NULL;
      char * store_file_output = NULL;

      //need to consider condition like cat >> output.txt, >> in args is ">>"

      for (int i = 0; args[i] != NULL; i++)  
      {
        if (strcmp(args[i],">")==0)
        {
          flag_output=true;
          count_output++;
        }
        if (strcmp(args[i],"<")==0)
        {
          flag_input=true;
          count_input++;
        }
        if (strcmp(args[i],">>")==0)
        {
          flag_double_output=true;
          count_double_output++;
        }
      }
      //if there are both < and > : flag_input && flag_output

      //multiple < or > in the command, both > and >> in command
      if (count_input>1||count_output>1||count_double_output>1||(flag_output&&flag_double_output)){
        fprintf(stderr, "Error: invalid command\n");
        continue;
      }
      ///Built-in commands (e.g., cd) cannot be I/O redirected or piped.
      if ((flag_cd&&flag_pipes)||(flag_cd&&flag_output)||(flag_cd&&flag_double_output)||(flag_cd&&flag_input)){
        fprintf(stderr, "Error: invalid command\n");
        continue;
      }

      if ((flag_jobs&&flag_pipes)||(flag_jobs&&flag_output)||(flag_jobs&&flag_double_output)||(flag_jobs&&flag_input)){
        fprintf(stderr, "Error: invalid command\n");
        continue;
      }

      if ((flag_fg&&flag_pipes)||(flag_fg&&flag_output)||(flag_fg&&flag_double_output)||(flag_fg&&flag_input)){
        fprintf(stderr, "Error: invalid command\n");
        continue;
      }

      //nothing after exit is allowed, already have it below in exit section

      //need to later be used in special IO case
      int address_input=0;
      int address_output=0;

      if( flag_input || flag_output || flag_double_output){
        bool invalid_redirect = false;
        //move them outside if loop to be used in later lines
        // char * store_file_input = NULL;
        // char * store_file_output = NULL;


        for (int i = 0; args[i] != NULL; i++) 
        {
            
          if (strcmp(args[i], ">") == 0) 
          {
            //need to later consider what if nothing after > and <
            if (args[i + 1] == NULL) {  
              fprintf(stderr, "Error: invalid command\n");
              invalid_redirect = true;
              break;
            }
            address_output=i;
            store_file_output=args[address_output+1];
            

          }
          if (strcmp(args[i], "<") == 0) 
          {
            //need to consider what if nothing after > and <
            if (args[i + 1] == NULL) {  
              fprintf(stderr, "Error: invalid command\n");
              invalid_redirect = true;
              break;}

            address_input=i;
            store_file_input=args[address_input+1];

          }
          if (strcmp(args[i], ">>") == 0) 
          {
            //need to consider what if nothing after > and <
            if (args[i + 1] == NULL) {  
              fprintf(stderr, "Error: invalid command\n");
              invalid_redirect = true;
              break;
            }
            address_output=i;
            store_file_output=args[address_output+1];
            
            
          }
          

        }   
        if (invalid_redirect) {
          continue;
        }
      }
    //cannot use == there, because both are pointers comparing address, we use strcmp()
    //int strcmp(const char *s1, const char *s2);
    //return an integer less than, equal to, or greater than zero if s1 (or the
    //first n bytes thereof) is found, respectively, to be less than, to match, or be greater than s2.

    //consider special built-in command here
    if (strcmp(args[0], "cd") == 0){
      //directly give arg to the chdir() to handle
      if (args[1] == NULL || args[2]!=NULL) {
        //error message should be sent to stderr by fprintf
        fprintf(stderr, "Error: invalid command\n");} 
        else 
        {
        /**
       * chdir()
       *  int chdir(const char *path);
       * can automatically deal with absolute/relative path
       * chdir() changes the current working directory of the calling process to the directory specified in path.
       * On success, zero is returned.  On error, -1 is returned
       */
        int test= chdir(args[1]);
        if (test != 0) 
        {
          fprintf(stderr, "Error: invalid directory\n");
        }
        }
      
      

    }
    else if (strcmp(args[0], "exit") == 0)
    {
      bool job_index_exist=(joblist_index >= 0);

      
      //if there are args after exit
      if (args[1] != NULL) {
        fprintf(stderr, "Error: invalid command\n");
        continue;
      }
      //if there are suspended job
      else if (job_index_exist) {
        fprintf(stderr, "Error: there are suspended jobs\n");
        continue;
      }

      else{
        //only break for correct exit command
        break;
      }
    }


    //jobs
    else if (strcmp(args[0],"jobs")==0){


      //if there are args after jobs
      if (args[1] != NULL) {
        fprintf(stderr, "Error: invalid command\n");
        //no need for break here, will go back to start of while loop after this line
        //break;
        continue;
      }
      
      
      for(int i=0; i<joblist_index+1; i++){
        printf("[%d] %s\n", i+1, joblist_user_lines[i]);
      }

      }

      //fg
    else if (strcmp(args[0],"fg")==0){

      if (args[1] == NULL || args[2]!=NULL) {
        //error message should be sent to stderr by fprintf
        fprintf(stderr, "Error: invalid command\n");
        continue;
      } 

      pid_t temp_pid =0;
      char *endptr = NULL;
      long parsed_index = strtol(args[1], &endptr, 10);
      if (endptr == args[1] || *endptr != '\0' || parsed_index <= 0) {
        fprintf(stderr, "Error: invalid command\n");
        continue;
      }
      int temp_index=(int)parsed_index;
      int count_for_temp_index=0;
      int final_index_for_the_command=0;
      bool job_index_exist=false;

      for (int i=0; i<joblist_index+1; i++){
        count_for_temp_index++;
        if(count_for_temp_index==temp_index){
          temp_pid=joblist[i];
          job_index_exist=true;
          final_index_for_the_command=i;
          break;
        }
      }
      if (!job_index_exist){
        fprintf(stderr, "Error: invalid job\n");
        continue;}
      
      if (kill(temp_pid, SIGCONT) < 0) {
        fprintf(stderr, "Error: invalid job\n");
        continue;
      }
      
      int status = 0;
      bool stopped_in_fg = false;
      pid_t wpid = waitpid(temp_pid, &status, WUNTRACED);
      if (wpid > 0 && WIFSTOPPED(status)) {
        stopped_in_fg = true;
      }

      //why need to evaluate status there?-->if this fg if end,while loop start again,
      //but the command is re-suspended, shell cannot take the stdin back so next loop command is blocked
      //and since fg is in the parent shell, after this if, the joblist will be lost if just delete it in fg without evaluating it
      if (stopped_in_fg){
          //when a resumed job is suspended again, move it to the end of the list
          pid_t resumed_pid = temp_pid;
          char resumed_cmd[1000];
          strcpy(resumed_cmd, joblist_user_lines[final_index_for_the_command]);
          for (int i = final_index_for_the_command; i < joblist_index; i++) {
            joblist[i] = joblist[i + 1];
            strcpy(joblist_user_lines[i], joblist_user_lines[i + 1]);
          }
          joblist[joblist_index] = resumed_pid;
          strcpy(joblist_user_lines[joblist_index], resumed_cmd);
          continue;
          // joblist_index++;
          // //a separate array for pid store, index same as joblist_user_lines
          // joblist[joblist_index]=temp_pid;

          // //command part(for each)
          // char edit_joblist[1000]=joblist_user_lines[final_index_for_the_command];

          // //joblist_user_lines[joblist_index]=edit_joblist cannot work bc edit_joblist is local variable, vanish after exit
          // strcpy(joblist_user_lines[joblist_index], edit_joblist);

          // //handle extra /n at the end of userlines, replace it with \0
          // edit_joblist[strcspn(joblist_user_lines[final_index_for_the_command], "\n")] = '\0';

            
    }
    //delete it in joblist and keep list compact so index is stable
    for (int i = final_index_for_the_command; i < joblist_index; i++) {
      joblist[i] = joblist[i + 1];
      strcpy(joblist_user_lines[i], joblist_user_lines[i + 1]);
    }
    if (joblist_index >= 0) {
      joblist[joblist_index] = 0;
      joblist_user_lines[joblist_index][0] = '\0';
      joblist_index--;
    }



    }

  
    

    

    /**pipes */

    else if (count_pipes > 0) {
      
      if(flag_input||flag_double_output||flag_output){
        // < before the first pipe,  > />> after the last pipe
        int first_pipe=pipe_positions[0];
        int last_pipe=pipe_positions[count_pipes-1];

        bool input_position_invalid = flag_input && (address_input > first_pipe);
        bool output_position_invalid = (flag_output || flag_double_output) && (address_output < last_pipe);
        if(input_position_invalid || output_position_invalid){
          fprintf(stderr, "Error: invalid command\n");
          continue;
        }
        }

        // first split args according to | ，then edit args(set thing of <>>> and thing after as NULL) for it to execv command like ls cat

        //first is for count command numbers it split to, second is to record all args[i] in this command
        char* split_command[100][100];
        int commands_index=0;
        int component_in_command=0;
        bool invalid_pipe_syntax=false;

        
        //no split method in C, so have to do it manually
        //prevent ?? | ls and ls | ??
        for(int i=0; args[i]!=NULL; i++){
          if(strcmp(args[i],"|")==0){
            if (component_in_command == 0) {
              invalid_pipe_syntax = true;
              break;
            }
            //end this command
            split_command[commands_index][component_in_command]=NULL;
            commands_index++;
            component_in_command=0;

          }
          else{
            
            split_command[commands_index][component_in_command]=args[i];
            component_in_command++;
          }
        }
        if (invalid_pipe_syntax || component_in_command == 0) {
          fprintf(stderr, "Error: invalid command\n");
          continue;
        }

        //already args[i]=NULL so exit for loop, add the NULL at the end as a whole
        split_command[commands_index][component_in_command]=NULL;


 

        // edit args like below for IO redirection
        
        // edit the first program 
        if (flag_input) {
          for (int i = 0; split_command[0][i] != NULL; i++) {
              if (strcmp(split_command[0][i], "<") == 0) {
                split_command[0][i] = NULL;  
                break;
              }
            }
            }
    
        // edit the last program > / >>
        if (flag_output||flag_double_output) {
            for (int i = 0; split_command[commands_index][i] != NULL; i++) {
                if (strcmp(split_command[commands_index][i], ">") == 0 || 
                    strcmp(split_command[commands_index][i], ">>") == 0) 
                    {
                    split_command[commands_index][i] = NULL;  
                    break;
                    }
            }
        }

      
      
      //sample command: cat < input.txt | cat | cat >> output.txt, cat|cat, cat|cat|cat

  
        
        // create pipes, eg.pipe=1, two command since later loop start from 0
        int pipes[count_pipes][2];
        pid_t pipe_pids[101];
        for (int i = 0; i < count_pipes; i++) {
            if (pipe(pipes[i]) == -1) {
               perror("pipe");
               continue;
           }
        }
        

        //Fork for every command, sample:cat|cat, cat|cat|cat
        for (int i = 0; i < count_pipes+1; i++) {
            pid_t pid_in_pipes = fork();
            
            if (pid_in_pipes == 0) {
              //child should handle terminal signals normally
              signal(SIGINT, SIG_DFL);
              signal(SIGQUIT, SIG_DFL);
              signal(SIGTSTP, SIG_DFL);
              signal(SIGTTOU, SIG_DFL);
              signal(SIGTTIN, SIG_DFL);
              //eg. stdin-->pd[0][0],read from pipes,pipes already filled by later if statement
              //if not the first command, read input from output from read [0](the result of previous several pipes)
              if (i > 0) {
                    dup2(pipes[i-1][0], 0);
                }
              //last one does not need pipe, directly output to screen(default)
              //eg. stdout--->pd[0][1], first one run when i=0
              if (i < count_pipes) {
                    dup2(pipes[i][1], 1);
                }
                
                
                
              // do redirection
              //sample command: cat < input.txt | cat | cat >> output.txt
              if ((i==0)&&(flag_input)){
                //read only
                int fd = open(store_file_input, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "Error: invalid file\n");
                    exit(1);
                }
                dup2(fd, 0);  // redirect stdin to file fd
                close(fd);
              }

              if ((i==count_pipes)&&(flag_output)){
                //write in | create | clear
                int fd = open(store_file_output, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                    fprintf(stderr, "Error: invalid file\n");
                    exit(1);
                }
                dup2(fd, 1);  // redirect stdout to file fd
                close(fd);
          }
              // close all the pipes
              for (int i = 0; i < count_pipes; i++) {
                    close(pipes[i][0]);
                    close(pipes[i][1]);
              }

              if ((i==count_pipes)&&(flag_double_output)){
                //write in | create | clear
                int fd = open(store_file_output, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                    fprintf(stderr, "Error: invalid file\n");
                    exit(1);
                }
                dup2(fd, 1);  // redirect stdout to file fd
                close(fd);
              }

              


              
              /**
               * execv for every command
               */
              if (split_command[i][0][0] == '/') {
                // absolute path like /bin/ls -l
                execv(split_command[i][0], split_command[i]);

                  
              } else if (strchr(split_command[i][0], '/') != NULL) {
                /**
                 * The strchr() functions return a pointer to the matched character or NULL if the character is not
                 * search if / exist in arg[0], since /at the start has been considered, this scienario is / in args but not at start
                 */
                // relative path,like dir1/dir2/program is equivalent to ./dir1/dir2/program
                execv(split_command[i][0], split_command[i]);


                  
              } else {
                // specifies only the base name without any slash,search under /usr/bin need parsing
                char edit_path_usr_bin[1000];
                char edit_path_bin[1000];
                //try common system binary locations on different UNIX-like systems
                sprintf(edit_path_usr_bin, "%s%s", "/usr/bin/", split_command[i][0]);
                execv(edit_path_usr_bin, split_command[i]);
                sprintf(edit_path_bin, "%s%s", "/bin/", split_command[i][0]);
                execv(edit_path_bin, split_command[i]);
                fprintf(stderr, "Error: invalid program\n");
                exit(1);

              }
                
            
            }
            else if (pid_in_pipes > 0) {
              pipe_pids[i] = pid_in_pipes;
            }
        
        
        
      }
      //after the for loop
      // shell close all the pipes, need first close bc child process need to wait eof to end
        for (int i = 0; i < count_pipes; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
       
        bool pipeline_stopped = false;
        // wait all the child process
        for (int i = 0; i < count_pipes+1; i++) {
            int status = 0;
            if (waitpid(pipe_pids[i], &status, WUNTRACED) > 0 && WIFSTOPPED(status)) {
              pipeline_stopped = true;
            }
        }
        if (pipeline_stopped) {
          joblist_index++;
          joblist[joblist_index] = pipe_pids[0];
          char edit_joblist[1000];
          strcpy(edit_joblist, original_userline);
          int new_line_index=strcspn(edit_joblist, "\n");
          edit_joblist[new_line_index] = '\0';
          strcpy(joblist_user_lines[joblist_index], edit_joblist);
        }

    }

    else
    {
      //all other commands like ls, cat...
      
      pid_t child_pid = fork();
      if (child_pid < 0) 
      {
      // fork failed (this shouldn't happen)
      perror("fork");
      } 
      else if (child_pid == 0) 
      {
      // child (new process)
      //cannot use execvp, only use execv()
      //different path types
        //child should receive terminal signals by default
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
      
      
        //edit args
        for (int i = 0; args[i] != NULL; i++) 
        {
          if (strcmp(args[i], ">") == 0) 
          {
            args[i]=NULL;
            break;
          }
        }

        for (int i = 0; args[i] != NULL; i++) 
        {
          if (strcmp(args[i], ">>") == 0) 
          {
            args[i]=NULL;
            break;
          }
        }

        for (int i = 0; args[i] != NULL; i++) 
        {
          if (strcmp(args[i], "<") == 0) 
          {
            args[i]=NULL;
            break;
          }
        }
      

      //really do redirection
      //output redirection need to write thing in file
      if (flag_output){
        //write in | create | clear
        int fd = open(store_file_output, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            fprintf(stderr, "Error: invalid file\n");
            exit(1);
        }
        dup2(fd, 1);  // redirect stdout to file fd
        close(fd);
      }

      if (flag_double_output){
        //write in | create | clear
        int fd = open(store_file_output, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            fprintf(stderr, "Error: invalid file\n");
            exit(1);
        }
        dup2(fd, 1);  // redirect stdout to file fd
        close(fd);
      }

      if (flag_input){
        //read only
        int fd = open(store_file_input, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Error: invalid file\n");
            exit(1);
        }
        dup2(fd, 0);  // redirect stdin to file fd
        close(fd);
      }

      

      


      if (args[0][0] == '/') {
        // absolute path like /bin/ls -l
        execv(args[0], args);

          
      } else if (strchr(args[0], '/') != NULL) {
        // relative path,like dir1/dir2/program is equivalent to ./dir1/dir2/program
        /**
         * The strchr() functions return a pointer to the matched character or NULL if the character is not
         * search if / exist in arg[0], since /at the start has been considered, this scienario is / in args but not at start
         */
        execv(args[0], args);

      // cat usr/bin/cat velongs to the below scenario, and should not be affected?
          
      } else {
        // specifies only the base name without any slash,search under /usr/bin need parsing
        char edit_path_usr_bin[1000];
        char edit_path_bin[1000];
        //try common system binary locations on different UNIX-like systems
        sprintf(edit_path_usr_bin, "%s%s", "/usr/bin/", args[0]);
        execv(edit_path_usr_bin, args);
        sprintf(edit_path_bin, "%s%s", "/bin/", args[0]);
        execv(edit_path_bin, args);
        fprintf(stderr, "Error: invalid program\n");
        exit(1);

    }
        
      } 
    else
      {   
        // parent wait until child terminates
        //status is place to store info of child process termination
        int status;
        /**
        pid_t wait(int *wstatus);
        //options, if 0, only return when child end, if filled WUNTRACED, also return when it stopped
        pid_t waitpid(pid_t pid, int *wstatus, int options);
        int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
         */
        //store info into status, 
        //pid specify wait for which process, 0 means waiting
        waitpid(child_pid, &status, WUNTRACED);

        if (WIFSTOPPED(status)){
          joblist_index++;
          //a separate array for pid store, index same as joblist_user_lines
          joblist[joblist_index]=child_pid;

          //command part(for each)
          char edit_joblist[1000];
          strcpy(edit_joblist, original_userline);

          //handle extra /n at the end of userlines, replace it with \0
          //strcspn()return the first index of \n in  edit joblist
          int new_line_index=strcspn(edit_joblist, "\n");
          edit_joblist[new_line_index] = '\0';

          //joblist_user_lines[joblist_index]=edit_joblist cannot work bc edit_joblist is local variable, vanish after exit
          strcpy(joblist_user_lines[joblist_index], edit_joblist);

        }
        
          
        
        
      }
    }
    

        
       

  }
  free(userlines);
  return 0;
      
}




