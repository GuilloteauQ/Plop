#include <assert.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int get_my_rank() {
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    return rank;
}

int get_world_size() {
    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    return size;
}

void send_string(char* string, int string_lenght, int destination) {
    // int string_lenght = strlen(string);
    // First sending the size of the string
    // to allocate the correct size
    MPI_Send(&string_lenght, 1, MPI_INT, destination, 99, MPI_COMM_WORLD);
    // Then sending the string
    MPI_Send(string, string_lenght, MPI_CHAR, destination, 99, MPI_COMM_WORLD);
}

char* receive_string(int source, int* string_lenght) {
    MPI_Recv(string_lenght, 1, MPI_INT, source, 99, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    char* string = malloc(sizeof(char) * (*string_lenght));
    MPI_Recv(string, *string_lenght, MPI_CHAR, source, 99, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    return string;
}

void start_master(char* filename, char* epilogue_command) {
    int world_size = get_world_size();
    int ready_status = 0;
    FILE* fp;
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    int free_node;

    fp = fopen(filename, "r");
    if (fp == NULL)
        exit(EXIT_FAILURE);

    while ((read = getline(&line, &len, fp)) != -1) {
        MPI_Recv(&free_node, 1, MPI_INT, MPI_ANY_SOURCE, 42, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        assert(free_node > 0 && free_node < world_size);
        line[strcspn(line, "\n")] = 0;
        fprintf(stderr, "\033[31m[MASTER]\033[39m Running on node %d ('%s')\n",
                free_node, line);
        MPI_Send(&ready_status, 1, MPI_INT, free_node, 99, MPI_COMM_WORLD);
        send_string(line, len, free_node);
    }

    fclose(fp);
    if (line)
        free(line);

    // Tell the nodes it is over
    int end_status = (epilogue_command == NULL) ? -2 : -1;
    for (int i = 1; i < world_size; i++) {
        fprintf(stderr,
                "\033[31m[MASTER]\033[39m Tell Node %d the campaign is over: "
                "Execute epilogue\n",
                i);
        MPI_Send(&end_status, 1, MPI_INT, i, 99, MPI_COMM_WORLD);
        if (epilogue_command != NULL)
            send_string(epilogue_command, strlen(epilogue_command) + 1, i);
    }
}

void master_distribute_prologue(char* command) {
    int world_size = get_world_size();
    int ready_status = 0;
    for (int i = 1; i < world_size; i++) {
        fprintf(stderr,
                "\033[31m[MASTER]\033[39m Sending prologue to Node %d\n", i);
        MPI_Send(&ready_status, 1, MPI_INT, i, 41, MPI_COMM_WORLD);
        send_string(command, strlen(command) + 1, i);
    }
}

void slave_execute_command() {
    // Receive the command as a string
    int string_lenght = 0;
    char* string = receive_string(0, &string_lenght);
    int status = system(string);
    assert(status != -1);
    free(string);
}

void slave_execute_prologue() {
    int status;
    MPI_Recv(&status, 1, MPI_INT, 0, 41, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    assert(status == 0);
    fprintf(stderr, "\033[%dm[Node %d]\033[39m Executing prologue\n",
            (get_my_rank() % 5) + 31, get_my_rank());
    slave_execute_command();
}

void tell_the_boss_i_am_free(int my_rank) {
    fprintf(stderr, "\033[%dm[Node %d]\033[39m Telling boss i am free\n",
            (my_rank % 5) + 31, my_rank);
    MPI_Send(&my_rank, 1, MPI_INT, 0, 42, MPI_COMM_WORLD);
}

void start_slave(int my_rank) {
    tell_the_boss_i_am_free(my_rank);
    int status;
    MPI_Recv(&status, 1, MPI_INT, 0, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (status == 0) {
        fprintf(stderr, "\033[%dm[Node %d]\033[39m Executing job\n",
                (my_rank % 5) + 31, my_rank);
        slave_execute_command();
        start_slave(my_rank);
    } else if (status == -1) {
        fprintf(stderr, "\033[%dm[Node %d]\033[39m Executing Epilogue\n",
                (my_rank % 5) + 31, my_rank);
        slave_execute_command();
        fprintf(stderr, "\033[%dm[Node %d]\033[39m Ok ! Going back home !\n",
                (my_rank % 5) + 31, my_rank);
    } else {
        fprintf(stderr, "\033[%dm[Node %d]\033[39m Ok ! Going back home !\n",
                (my_rank % 5) + 31, my_rank);
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int my_rank = get_my_rank();
    int arg_index = 1;
    char* filename = NULL;
    char* prologue = NULL;
    char* epilogue = NULL;

    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-f") == 0 ||
            strcmp(argv[arg_index], "--file") == 0) {
            assert(arg_index + 1 < argc && argv[arg_index + 1][0] != '-');
            filename = argv[arg_index + 1];
            arg_index = arg_index + 2;
        } else if (strcmp(argv[arg_index], "-p") == 0 ||
                   strcmp(argv[arg_index], "--prologue") == 0) {
            assert(arg_index + 1 < argc && argv[arg_index + 1][0] != '-');
            prologue = argv[arg_index + 1];
            arg_index = arg_index + 2;
        } else if (strcmp(argv[arg_index], "-e") == 0 ||
                   strcmp(argv[arg_index], "--epilogue") == 0) {
            assert(arg_index + 1 < argc && argv[arg_index + 1][0] != '-');
            epilogue = argv[arg_index + 1];
            arg_index = arg_index + 2;
        } else {
            fprintf(stderr, "Unkown argument: %s\n", argv[arg_index]);
            arg_index++;
        }
    }
    if (filename == NULL) {
        if (my_rank == 0)
            fprintf(stderr, "Must provide a campaign file ! (-f [file])\n");
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    if (my_rank == 0) {
        if (prologue != NULL)
            master_distribute_prologue(prologue);
        start_master(filename, epilogue);
    } else {
        if (prologue != NULL)
            slave_execute_prologue();
        start_slave(my_rank);
    }

    MPI_Finalize();
    return 0;
}
