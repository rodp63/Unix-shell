#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>

/*
  Compilar con: gcc shell.c -o shell
  Ejecutar con: ./shell
*/

#define MAX_LINE 80
#define MAX_ARGS 40
#define HISTORY_PATH ".history"

int p_wait;
int in_file, out_file;
int saved_in, saved_out;
int in, out;
int pipe_ind;
int save_c;

void parseInput(char *command, char **args)
{
  int args_count = 0;
  int command_len = strlen(command);
  int arg_start = -1;
  for(int i = 0; i < command_len; i++)
  {
    if(command[i] == ' ' || command[i] == '\t' || command[i] == '\n')
    {
      if(arg_start != -1)
      {
        args[args_count++] = &command[arg_start];
        arg_start = -1;
      }
      command[i] = '\0';
    }
    else
    {
      if(command[i] == '&')
      {
        p_wait = 0;
        i = command_len;
      }
      if(arg_start == -1) arg_start = i;
    }
  }
  args[args_count] = NULL;
}

void checkFlags(char **args)
{
  for(int i = 0; args[i] != NULL; i++)
  {
    if(!strcmp(args[i], ">"))
    {
      if(args[i+1] == NULL)
        printf("Invalid command format\n");
      else
        out_file = i + 1;
    }
    if(!strcmp(args[i], "<"))
    {
      if(args[i+1] == NULL)
        printf("Invalid command format\n");
      else
        in_file = i + 1;
    }
    if(!strcmp(args[i], "|"))
    {
      if(args[i+1] == NULL)
        printf("Invalid command after |\n");
      else
        pipe_ind = i;
    }
  }
}

void manageHistory(char **args)
{
  FILE* h = fopen(HISTORY_PATH, "r");
  if(h == NULL)
  {
    printf("The history is empty\n");
  }
  else
  {
    if(args[1] == NULL)
    {
      char c = fgetc(h);
      while(c != EOF)
      {
        printf ("%c", c);
        c = fgetc(h);
      }
    }
    else if(!strcmp(args[1], "-c"))
    {
      save_c = 0;
      remove(HISTORY_PATH);
    }
    else
    {
      printf("[!] Invalid syntax");
    }
    fclose(h);
  }
}

void execute(char **args)
{
  if(execvp(args[0], args) < 0)
  {
    printf("[!] Command not found\n");
    exit(1);
  }
}

void saveCommand(char *command)
{
  FILE* h = fopen(HISTORY_PATH, "a+");
  fprintf(h, "%s", command);
  rewind(h);
}

int main(void)
{
  char command[MAX_LINE];
  char last_command[MAX_LINE];
  char parse_command[MAX_LINE];
  char *args[MAX_ARGS];
  char *argsp1[MAX_ARGS], *argsp2[MAX_ARGS];
  int should_run = 1, history = 0;
  int alert;
  int pipech[2];
  
  while(should_run)
  {
    printf("Jshell$ ");
    fflush(stdout);
    fgets(command, MAX_LINE, stdin);
    
    p_wait = 1;
    alert = 0;
    out_file = in_file = -1;
    pipe_ind = -1;
    save_c = 1;

    strcpy(parse_command, command);
    parseInput(parse_command, args);

    if(args[0] == NULL || !strcmp(args[0], "\0") || !strcmp(args[0], "\n")) continue;

    if(!strcmp(args[0], "exit"))
    {
      should_run = 0;
      continue;
    }

    if(!strcmp(args[0], "!!"))
    {
      if(history)
      {
        printf("%s", last_command);
        strcpy(command, last_command);
        strcpy(parse_command, command);
        parseInput(parse_command, args);
      }
      else
      {
        printf("No commands in history \n");
        continue;
      }
    }

    checkFlags(args);

    if(in_file != -1)
    {
      in = open(args[in_file], O_RDONLY);
      if(in < 0)
      {
        printf("Failed to open file \'%s\'\n", args[in_file]);
        alert = 1;
      }
      else
      {
        saved_in = dup(0);
        dup2(in, 0);
        close(in);
        args[in_file - 1] = NULL;
      }
    }
    
    if(out_file != -1)
    {
      out = open(args[out_file], O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
      if(out < 0)
      {
        printf("Failed to open file \'%s\'\n", args[out_file]);
        alert = 1;
      }
      else
      {
        saved_out = dup(1);
        dup2(out, 1);
        close(out);
        args[out_file - 1] = NULL;
      }
    }

    if(pipe_ind != -1)
    {
      int i = 0;
      for(; i < pipe_ind; i++) argsp1[i] = args[i];
      argsp1[i] = NULL;
      i++;
      for(; args[i] != NULL; i++) argsp2[i-pipe_ind-1] = args[i];
      argsp2[i] = NULL;
    }
    
    if(!alert && should_run)
    {
      if(!strcmp(args[0], "history")) manageHistory(args);
      else
      {
        if(!strcmp(args[0], "stop") || !strcmp(args[0], "continue"))
        {
          args[2] = args[1];
          args[1] = strcmp(args[0], "stop") ? "-SIGCONT" : "-SIGSTOP";
          args[0] = "kill";
          args[3] = NULL;
        }
        if(fork() == 0)
        {
          if(pipe_ind != -1)
          {
            pipe(pipech);
            if(fork() == 0)
            {
              saved_out = dup(1);
              dup2(pipech[1], 1);
              close(pipech[0]);
              execute(argsp1);
            }
            else
            {
              wait(NULL);
              saved_in = dup(0);
              dup2(pipech[0], 0);
              close(pipech[1]);
              execute(argsp2);
            }
          }
          else
            execute(args);
        }
        else
        {
          if(p_wait) wait(NULL);
        }
      }
      strcpy(last_command, command);
      if(save_c) saveCommand(command);
      history = 1;
    }
    dup2(saved_out, 1);
    dup2(saved_in, 0);
  }
  return 0;
}
