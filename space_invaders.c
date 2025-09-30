#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

int main(int argc, char *argv[]) {
    int rank, size;
    int nrows, ncols;

    // 1. Initialize MPI
    MPI_Init(&argc, &argv);

    // 2. Get total processes and my rank
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // 3. Parse command line args
    if (argc == 3) {
        nrows = atoi(argv[1]);
        ncols = atoi(argv[2]);
    } else {
        if (rank == 0) {
            printf("Usage: mpirun -np X ./program nrows ncols\n");
        }
        MPI_Finalize();
        return 0;
    }

    int required = 1 + nrows * ncols;

    if (size != required) {
        if (rank == 0) {
            printf("ERROR: need %d processes (1 player + %d invaders), but got %d\n",
                required, nrows*ncols, size);
        }
        MPI_Finalize();
        return 0;
    }

    if (rank == 0) {
        printf("OK: Using %d processes (1 player + %d invaders)\n",
            size, nrows*ncols);
    }


    // Debug print
    if (rank == 0) {
        printf("Grid requested: %d x %d with %d processes\n", nrows, ncols, size);
    }

    MPI_Finalize();
    return 0;
}