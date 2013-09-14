
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "transport_meta.h"

char **append_parent_args(int argc, char ** argv, char **child_args)
{
    char **_args = (char **) malloc(sizeof(char*)*(argc + sizeof(child_args)));
    int i, j;
    for(i = 0; child_args[i] != NULL; i++)
    {
        _args[i] = child_args[i];
    }
    for(j = 1; j < argc; j++,i++)
    {
        int len;
        len = strlen(argv[j]) + 1;
        _args[i] = (char *) malloc(sizeof(char)*len);
        strcpy(_args[i], argv[j]);
    }
    _args[i] = (char *) NULL;
    return _args;
}
int main(int argc, char ** argv)
{
    pid_t parent_pid = 0;
    pid_t server_pid = 0;
    pid_t client_pid = 0;
    int cl_status = -1;
    int sr_status = -1;
    char **_args;
    siginfo_t server_sigs;

    char * server_args[] =
    {
        "server",
        "9112",
        (char *) NULL
    };
    char * client_args[] =
    {
        "client",
        "127.0.0.1",
        "9112",
        (char *) NULL
    };



    if( (parent_pid = fork()) == 0  )
    {
        _args = append_parent_args(argc,argv,server_args);
        printf("Starting server pid %ld\n", (long) getpid());
        execvp("./server", _args);
        perror("execvp");
        exit(1);
    } else {
        do {
            bzero(&server_sigs, sizeof(server_sigs));
            server_sigs.si_signo = SIGCHLD;
            if(waitid(P_PID,parent_pid, &server_sigs, WEXITED | WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT) < 0)
            {
                perror("waitid");
                goto error_kill_server;
            }
        } while(server_sigs.si_pid != 0);
        server_pid = server_sigs.si_pid;

        if( (parent_pid = fork()) == 0  )
        {
            _args = append_parent_args(argc,argv,client_args);
            printf("Starting client pid %ld\n", (long) getpid());
            execvp("./client", _args);
            perror("execvp");
            exit(1);
        }


    }

    if(waitpid(client_pid,&cl_status,0) < 0)
        perror("waitpid");
    else
        fprintf(stderr,"Client returned status %d\n", cl_status);
error_kill_server:
    if( kill(server_pid,SIGABRT) < 0 )
    {
        perror("kill");
        goto on_error;
    }

    if( waitpid(server_pid,&sr_status,0) < 0 )
        perror("waitpid");
    else
        fprintf(stderr,"Server errored with status %d\n", sr_status);

on_error:
    fprintf(stderr,"Parent exiting\n");
    exit(EXIT_FAILURE);

    fprintf(stderr,"Server caught signal %d\n", WTERMSIG(sr_status));
    kill(client_pid,SIGABRT);

    if(waitpid(client_pid,&cl_status,0) < 0)
        perror("waitpid");
    else
        fprintf(stderr,"Client returned status %d\n", WEXITSTATUS(cl_status));
    fprintf(stderr,"Parent exiting\n");
    exit(EXIT_FAILURE);
}
