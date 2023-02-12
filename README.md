# Mini shell

Mini shell is an interactive shell program run on **Linux** using the creation, management, and coordination of multiple processes, imitating the Unix shell. I got full marks on this assignment from the Operating System course.

# Features

Mini shell supports the following features,

1. Execute a command with any number of arguments with child processes.
2. Handle various errors such as incorrect filenames, invalid arguments, not binary file, etc.
3. Remove all zombie processes.
4. Execute any number of commands connected by **pipes** with any number of arguments.
5. Handle `SIGINT`, `SIGUSR1`, and `SIGCHLD` signal.
6. Handle **background jobs**.
7. Support `timeX` command to report process statistics.

# Usage

1. First clone the repository in **Linux** environment.
2. Compile it with `gcc -o mini_shell mini_shell.c`.
3. Execute: `./mini_shell`.
