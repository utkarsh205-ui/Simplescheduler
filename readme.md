# Simple Scheduler
**Simple Scheduler** is a command-line shell with a built-in task scheduler. It allows users to execute shell commands and submit tasks for execution. The task scheduler efficiently manages task execution, tracks their execution time, and provides a history of completed tasks.

## Features
- Command-line interface for executing shell commands.
- Task submission and execution.
- Task scheduling with round-robin processing.
- History tracking of executed tasks, including execution time and command details.

## Usage
- Run the SimpleScheduler daemon, specifying the number of CPUs (NCPU) and the time slice (TSLICE) as arguments.
```
./SimpleScheduler <NCPU> <TSLICE>
```
- Execute shell commands directly within the shell.
- Submit tasks using the `submit` command. For example:
```
submit ./your_execfile
```
- Keep in mind the following rules for executable files
1. Not allowed to submit any program that has blocking calls, e.g., scanf, sleep, etc.
2. The user program cannot have any command line parameters.

- Terminate the shell and view the final task execution details using Ctrl+C.