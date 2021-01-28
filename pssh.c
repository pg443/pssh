#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <readline/readline.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>		/*errno							*/

#include "builtin.h"
#include "parse.h"

/*******************************************
 * Set to 1 to view the command line parse *
 *******************************************/
#define DEBUG_PARSE 0

#define READ_SIDE 0
#define WRITE_SIDE 1

#define MAXJOBS 100

void set_fg_pgid (pid_t pgid);

pid_t parent;
typedef enum{STOPPED, TERM, BG, FG} JobStatus;
typedef struct{
    char* name;
    unsigned int npids;
    pid_t *pids;
    pid_t pgid;
    JobStatus status;
} Job;
Job job_array[MAXJOBS];

void write_name(Parse* P, int job){
    int t; job_array[job].name = (char *)malloc(P->ntasks * 100);
    for (t = 0; t< P->ntasks; t++){
        int i = 0;
        while(P->tasks[t].argv[i] != NULL){
            strcat(job_array[job].name, P->tasks[t].argv[i]);
            strcat(job_array[job].name," ");
            i++;
        }
        if (t+1 != P->ntasks){
            strcat(job_array[job].name, "| ");
        }
    }
    if (P->background)
        strcat(job_array[job].name, "&");
}
int pcs_grp_init(int ntasks){
    Job job;
    job.npids = ntasks;
    job.pids = malloc(ntasks * (sizeof(pid_t) + 1));
    memset(job.pids, 0, ntasks * sizeof(pid_t));
    job.status = FG;
    job.name = NULL;
    job.pgid = 0;

    int i =0;
    while(job_array[i].npids != 0)
        i++;
    job_array[i] = job;
    return i;
}
int is_job_valid(int job){
    int i;
    for (i = 0; i< MAXJOBS; i++){
        if (job_array[i].pgid != 0) {
            if(i == job)
                return 1;
        }
    }
    return 0;
}
void del_job(int job_num){
    free(job_array[job_num].pids);
    free(job_array[job_num].name);
    job_array[job_num].status = TERM;
    job_array[job_num].npids = 0;
}

pid_t find_pgid(pid_t pid){
    int i;
    for (i=0; i<MAXJOBS; i++){
        int j = 0;
        if (job_array[i].npids == 0)
            continue;
        while(job_array[i].pids[j] != 0){
            if (job_array[i].pids[j] == pid){
                return job_array[i].pgid;
            }
            j++;
        }
    }
    return -1;
}

int find_job(pid_t pid){
    pid_t pgid;
    pgid = find_pgid(pid);
    int i = 0;
    while (job_array[i].pgid != pgid)
        i++;
    return i;
}
void print_banner ()
{
    printf ("                    ________   \n");
    printf ("_________________________  /_  \n");
    printf ("___  __ \\_  ___/_  ___/_  __ \\ \n");
    printf ("__  /_/ /(__  )_(__  )_  / / / \n");
    printf ("_  .___//____/ /____/ /_/ /_/  \n");
    printf ("/_/ Type 'exit' or ctrl+c to quit\n\n");
}
int count_args(char* argv[]){
    int i = 0;
    while(argv[i] != NULL){
        i++;
    }
    return i;
}

void killhandler(int sig){
    fprintf(stderr, "\nInvalid use of kill command\n");
    exit(EXIT_SUCCESS);
}

int send_signal(char* argv[])
{   
    void (*old_sigsegv_handler)(int sig);
    old_sigsegv_handler = signal(SIGSEGV, killhandler);
    int argc = count_args(argv);
    int i;
    pid_t child;
	if (argc<2){
        printf("Usage: kill  [-s <signal>] <pid> | %c<job> ...\n",'%');	
		return 0;
	}
    switch(child = fork()){
        case -1:
            fprintf(stderr, "Fork failed\n");
            break;
        case 0:
            if (strcmp(argv[1], "-s")==0){
                if (atoi(argv[2])==0){
                    for(i = 3; i<argc; i++){
                        kill(atoi(argv[i]), atoi(argv[2]));
                        if (errno==0){
                            printf("PID %s exist and is able to recieve signals\n", argv[i]);
                        }else if(errno==1){
                            printf("PID %s exists, but we can't send it signals\n", argv[i]);
                        }else if (errno==3){
                            printf("PID %s does not exist\n", argv[i]);
                        }
                    }
                }else if(strstr(argv[3], "%") != NULL){
                    for(i = 3; i<argc; i++){
                        char* temp = argv[i];
                        temp++;
                        int holder;
                        holder = atoi(temp);
                        if(is_job_valid(holder)){
                            kill(-job_array[holder].pgid, SIGINT); 
                        }else{
                            fprintf(stderr, "pssh:  invalid job number:  [%d]\n", holder);
                        }
                    }
                }else{
                    for(i=3; i<argc; i++){
                        kill(atoi(argv[i]), 0);
                        if (errno==3){
                            printf("pssh:  invalid pid:  [%s]\n", argv[i]);
                        }else{
                            kill(atoi(argv[i]), atoi(argv[2]));
                        }
                    }
                }
            }else if  (strstr(argv[1], "%") != NULL){
                for(i=1; i<argc;i++){
                    char* temp = argv[i];
                    temp++;
                    int holder;
                    holder = atoi(temp);
                    if(is_job_valid(holder)){
                        kill(-job_array[holder].pgid, SIGINT); 
                    }else{
                        fprintf(stderr, "pssh:  invalid job number:  [%d]\n", holder);
                    }                   
                }
            }else{
                for(i=1; i<argc;i++){
                    kill(atoi(argv[i]), 0);
                    if (errno==3){
                        printf("pssh:  invalid pid:  [%s]\n", argv[i]);
                    }else{
                        kill(atoi(argv[i]), SIGINT);  
                    }                  
                }
            }
            exit(EXIT_SUCCESS);
        default:
            waitpid(child, NULL, 0);
    }
    signal(SIGSEGV, old_sigsegv_handler);
    return 0;
}


void foreground(char* argv[]){
    if (strstr(argv[1], "%") != NULL){
        char* temp = argv[1];
        temp++;
        int holder = atoi(temp);
        if(is_job_valid(holder)){
            job_array[holder].status = FG;
            set_fg_pgid(job_array[holder].pgid);
            kill(-job_array[holder].pgid, SIGCONT);
        }else{
            fprintf(stderr, "pssh:  invalid job number:  [%d]\n",holder);
        }
    }else{
        fprintf(stderr, "pssh:  Syntax Error\n\tfg:  Usage:  fg %c<job number>\n",'%');
    }
}
void background(char* argv[]){
    if (strstr(argv[1], "%") != NULL){
        char* temp = argv[1];
        temp++;
        int holder = atoi(temp);
        if(is_job_valid(holder)){
            job_array[holder].status = BG;
            kill(-job_array[holder].pgid, SIGCONT); 
        }else{
            fprintf(stderr, "pssh:  invalid job number:  [%d]\n",holder);
        }
    }else{
        fprintf(stderr, "pssh:  Syntax Error\n\tfg:  Usage:  bg %c<job number>\n",'%');
    }
}

/* returns a string for building the prompt
 *
 * Note:
 *   If you modify this function to return a string on the heap,
 *   be sure to free() it later when appropirate!  */
static char* build_prompt ()
{
    char prompt[1024]="";
    getcwd(prompt, sizeof(prompt));
    printf(prompt);
    return  "$ ";

}


/* return true if command is found, either:
 *   - a valid fully qualified path was supplied to an existing file
 *   - the executable file was found in the system's PATH
 * false is returned otherwise */
static int command_found (const char* cmd)
{
    char* dir;
    char* tmp;
    char* PATH;
    char* state;
    char probe[PATH_MAX];

    int ret = 0;

    if (access (cmd, X_OK) == 0)
        return 1;

    PATH = strdup (getenv("PATH"));

    for (tmp=PATH; ; tmp=NULL) {
        dir = strtok_r (tmp, ":", &state);
        if (!dir)
            break;

        strncpy (probe, dir, PATH_MAX);
        strncat (probe, "/", PATH_MAX);
        strncat (probe, cmd, PATH_MAX);

        if (access (probe, X_OK) == 0) {
            ret = 1;
            break;
        }
    }

    free (PATH);
    return ret;
}
void set_fg_pgid (pid_t pgid)
{
    void (*old)(int);
    old = signal (SIGTTOU, SIG_IGN);
    tcsetpgrp (STDIN_FILENO, pgid);
    tcsetpgrp (STDOUT_FILENO, pgid);
    signal (SIGTTOU, old);
}

void handler(int sig)
{
    switch(sig){
        pid_t pid;
        case SIGINT:
            if ((pid = tcgetpgrp(STDOUT_FILENO)) != parent){
                kill(-pid, SIGINT);
                set_fg_pgid(parent);
                printf("\n");
                fflush(stdout);
            }else{
                printf("\n");
            }
            break;
        case SIGQUIT:
            if ((pid = tcgetpgrp(STDOUT_FILENO)) != parent){
                kill(-pid, SIGQUIT);
                set_fg_pgid(parent);
                printf("\n");
                fflush(stdout);
            }else{
                printf("\n");
            }
            break;
        case SIGTSTP:
            if ((pid = tcgetpgrp(STDOUT_FILENO)) != parent){
                kill(-pid, SIGTSTP);
                set_fg_pgid(parent);
                printf("\n");
                fflush(stdout);
            }else{
                printf("\n");
            }
            break;
        case SIGTTIN:
            if ((pid = tcgetpgrp(STDOUT_FILENO)) != parent){
                kill(-pid, SIGTTIN);
                set_fg_pgid(parent);
                printf("\n");
                fflush(stdout);
            }else{
                printf("\n");
            }
            break;
        case SIGTTOU:
            if ((pid = tcgetpgrp(STDOUT_FILENO)) != parent){
                kill(-pid, SIGTTOU);
                set_fg_pgid(parent);
                printf("\n");
                fflush(stdout);
            }else{
                printf("\n");
            }
            break;
        default:
            break;
    }
}


void childhandler(int sig)
{
    pid_t child;
    int status;
    char printing[1024] = "";
    while ((child = waitpid (-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        int job = find_job(child);
        if (WIFSTOPPED (status)) {
            set_fg_pgid (parent);
            job_array[job].status = STOPPED;
            sprintf(printing, "[%d]+ stopped\t%s\n",job, job_array[job].name);
            write(STDOUT_FILENO, printing, sizeof(printing));
            continue;
        }
        else if (WIFCONTINUED (status)) {
            if(job_array[job].status == STOPPED){
                job_array[job].status = BG;
                sprintf(printing, "[%d]+ continued\t%s\n",job, job_array[job].name);
                write(STDOUT_FILENO, printing, sizeof(printing));
            }
            continue;
        }
        else if (WIFEXITED(status)){
            set_fg_pgid(parent);
            if (job_array[job].status == BG){
                sprintf(printing, "[%d]+ done\t%s\n",job, job_array[job].name);
                write(STDOUT_FILENO, printing, sizeof(printing));                
            }
            del_job(job);
            continue;
        }
        else if (WIFSIGNALED(status)){
            set_fg_pgid(parent);
            switch (WTERMSIG(status)){
                case SIGINT:
                    sprintf(printing, "\n");
                    write(STDOUT_FILENO, printing, sizeof(printing));   
                    break;
                case SIGQUIT:
                    sprintf(printing, "\n");
                    write(STDOUT_FILENO, printing, sizeof(printing));
                    break;                    
                case SIGTERM:
                    sprintf(printing, "[%d]+ done\t%s\n",job, job_array[job].name);
                    write(STDOUT_FILENO, printing, sizeof(printing));                   
                    break;
                case SIGHUP:
                    sprintf(printing, "[%d]+ done\t%s\n",job, job_array[job].name);
                    write(STDOUT_FILENO, printing, sizeof(printing));                   
                    break; 
                default:
                    break;               
            }
            del_job(job);
            continue;
        }else{
            set_fg_pgid(parent);
        }
        fflush(stdout);
    }
}


/* Called upon receiving a successful parse.
 * This function is responsible for cycling through the
 * tasks, and forking, executing, etc as necessary to get
 * the job done! */
void execute_tasks (Parse* P)
{
    unsigned int t;
    sigset_t mask;
    int in, out, fd[2];
    void (*old_sigtstp_handler)(int sig);
    old_sigtstp_handler = signal(SIGTSTP, SIG_DFL);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    /* The first process should get its input from the original file descriptor 0.  */
    in = STDIN_FILENO; out = STDOUT_FILENO;
    if (strcmp(P->tasks[0].cmd,"kill")==0){
        send_signal(P->tasks[0].argv);
        return;
    }
    if (!strcmp(P->tasks[0].cmd, "fg")){
        int arg = count_args(P->tasks[0].argv);
        if (arg<2){
            printf("Usage: fg %c<job number>\n",'%');
            return;
        }
        foreground(P->tasks[0].argv);
        return;
    }else if (!strcmp(P->tasks[0].cmd, "bg")){
        int arg = count_args(P->tasks[0].argv);
        if (arg<2){
            printf("Usage: bg %c<job number>\n",'%');
            return;
        }
        background(P->tasks[0].argv);
        return;
    }else if (!strcmp(P->tasks[0].cmd, "jobs")){
        set_fg_pgid(parent);
        int flag = 0;
        for (t =0; t<MAXJOBS; t++){
            if(job_array[t].npids != 0){
                if(job_array[t].status == FG || job_array[t].status == BG){
                    flag = 1;
                    printf("[%d] + running\t%s\n", t, job_array[t].name);
                }
                else if(job_array[t].status == STOPPED){
                    printf("[%d] + stopped\t%s\n", t, job_array[t].name);
                    flag = 1;
                }
            }
        }
        if(!flag) printf("pssh: jobs:  no jobs found\n");
        return;
    }

    int job_num = pcs_grp_init(P->ntasks);
    for (t = 0; t < P->ntasks; t++) {
        if (is_builtin (P->tasks[t].cmd)) {
            builtin_execute (P->tasks[t]);
        }
        else if (command_found (P->tasks[t].cmd)) {

/*=====================================================================
*=====================================================================*/

            if (pipe(fd) == -1) {
                fprintf(stderr, "error -- failed to create pipe");
                break;
            }

            //First task
            if (t==0){
                if(P->infile){
                    int file_indescriptor;
                    file_indescriptor = open(P->infile, O_RDONLY);
                    if(file_indescriptor==-1){
                        fprintf(stderr,"FILE NOT FOUND\n");
                        break;
                    }
                    in = file_indescriptor;
                }
            }
            //Last task
            if (t+1 == P->ntasks){
                if(P->outfile){
                    int file_outdescriptor;
                    file_outdescriptor = open(P->outfile, O_WRONLY|O_TRUNC|O_CREAT, S_IRWXU);
                    if(file_outdescriptor==-1){
                        fprintf(stderr,"FILE CANNOT BE CREATED\n");
                        break;
                    }
                    close(fd[READ_SIDE]);
                    out = file_outdescriptor;
                }else{
                    close(fd[READ_SIDE]);
                    out = STDOUT_FILENO;
                }
            }
            // Not the last task
            else{
                out = fd[WRITE_SIDE];
            }

            job_array[job_num].pids[t] = fork();
            if (t==0){
                write_name(P, job_num);
                job_array[job_num].pgid = job_array[job_num].pids[0];                
            }
            setpgid(job_array[job_num].pids[t], job_array[job_num].pgid);

            switch(job_array[job_num].pids[t]){
            case -1:
                fprintf(stderr,"Fork failed\n");
                break;
            case 0:
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                if (in != STDIN_FILENO){
                    if (dup2 (in, STDIN_FILENO) == -1){
                        fprintf(stderr, "error -- dup2() failed for WRITE_SIDE -> STDIN");
                    }
                    close (in);
                }
                if (out != STDOUT_FILENO){
                    if (dup2 (out, STDOUT_FILENO) == -1){
                        fprintf(stderr, "error -- dup2() failed for WRITE_SIDE -> STDOUT");
                    }
                    close (out);
                }
                execvp(P->tasks[t].cmd, P->tasks[t].argv);
            default:
                break;
            }
            close (fd[WRITE_SIDE]);
            in = fd[READ_SIDE];

            if (!P->background){
                set_fg_pgid(job_array[job_num].pgid);
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
            }
            else{
                set_fg_pgid(parent);
                if (t==0)
                    printf("[%d] + running\t%s\n",job_num, job_array[job_num].name);
                job_array[job_num].status = BG;
                sigprocmask(SIG_UNBLOCK, &mask, NULL);
            }



/*======================================================================
*======================================================================*/
        }else {
            printf ("pssh: command not found: %s\n", P->tasks[t].cmd);
            del_job(job_num);
            break;
        }
    }
    signal(SIGTSTP, old_sigtstp_handler);
    return;
}               



int main (int argc, char** argv)
{
    setpgid(0,0);
    parent = getpgrp();
    signal(SIGTSTP, handler);
    signal(SIGQUIT, handler);
    signal(SIGINT, handler);
    signal(SIGTTIN, handler);
    signal(SIGTTOU, handler);
    signal(SIGCHLD, childhandler);



    char* cmdline;
    Parse* P;

    print_banner ();

    while (1) {
        while(tcgetpgrp(STDOUT_FILENO) != parent)
            pause();
        cmdline = readline (build_prompt());
        if (!cmdline)       /* EOF (ex: ctrl-d) */
            exit (EXIT_SUCCESS);

        P = parse_cmdline (cmdline);
        if (!P)
            goto next;

        if (P->invalid_syntax) {
            printf ("pssh: invalid syntax\n");
            goto next;
        }

#if DEBUG_PARSE
        parse_debug (P);
#endif

        execute_tasks (P);


    next:
        parse_destroy (&P);
        free(cmdline);
    }
}
