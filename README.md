# In-memory FAT File System Simulation

This project is a C-based simulation of a File Allocation Table (FAT) file system operating within a 64 MB shared-memory segment, which acts as a Virtual Disk (VD). It implements a custom shell interface to manage files and directories using raw memory operations.

## System Architecture

The simulation is modularized into three main components:

### 1. Disk Manager (`diskmanager.c`)
Responsible for creating, formatting, and removing the virtual disk in shared memory. 
- Initializes the metadata in Block 0.
- Sets up the free-block bitmap.
- Structures the FAT table.
- Creates an empty root directory. 

### 2. Disk Utilities (`diskutils.c`)
A core library handling low-level disk operations. 
- Manages block allocation (`getfreeblock`, `freeblock`).
- Handles directory creation and file copying.
- Performs raw data extraction using `memcpy`.

### 3. Foosh Shell (`foosh.c`)
The "Foo version of sh", an interactive command-line interpreter that attaches to the virtual disk. 
- Maintains the current working directory state.
- Processes user commands.

## Supported Commands

The `foosh` shell supports standard file system operations utilizing relative or absolute paths:

- `md` / `mkdir`: Creates a new directory.
- `cd` / `chdir`: Changes the current working directory.
- `ls`: Prints detailed directory metadata, including type, name, size, and starting block.
- `dir`: Lists only the names of the files and subdirectories.
- `cp` / `copy`: Transfers files between the host hard disk (using a ``` ` ``` prefix for the host) and the virtual disk, overwriting existing files if necessary.
- `prn` / `type`: Prints the text contents of a file to the terminal.
- `exit` / `quit`: Detaches from the virtual disk and terminates the shell session.

## Build Instructions

Use the provided `Makefile` to compile the components:

```bash
make all
```

## Running the Simulation

1. **Initialize the virtual disk (Creation mode: 0):**
   ```bash
   ./diskmanager 0
   ```

2. **Launch the interactive shell:**
   ```bash
   ./foosh
   ```

3. **Clean up the virtual disk after exiting the shell (Removal mode: 1):**
   ```bash
   ./diskmanager 1
   ```
