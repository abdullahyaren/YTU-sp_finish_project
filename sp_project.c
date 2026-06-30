/* BLM1031 - Structural Programming Term Project
   Number Matching Game on an N x N matrix (Numberlink / "flow" style puzzle)

   The program places each number 1..N exactly twice on an N x N matrix (either
   randomly or by reading a file) and lets the user connect the matching numbers
   with non-crossing paths so that no empty cell is left. The game supports a
   manual mode, an automatic solver, an undo / redo mechanism, saving / resuming
   a game and recording player performance.

   Coding rules followed:
     - Written in ANSI C (C90). Compile with: gcc -ansi -pedantic
     - No global variables, no static variables.
     - No "continue" and no "goto".
     - "break" is used only inside switch statements.
     - Dynamic memory allocation is used for every matrix and list.
     - lowerCamelCase for variables and functions, UPPERCASE for macros.
     - A comment describing inputs and outputs is placed before each function. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EMPTY 0
#define TRUE 1
#define FALSE 0
#define MAX_NAME_LENGTH 50
#define MODE_MANUAL 1
#define MODE_AUTO 2
#define SOURCE_RANDOM 0
#define SOURCE_FILE 1
#define SCORE_FILE "scores.txt"
#define SAVE_FILE "savegame.txt"
#define MAX_RECORDS 1000
#define MIN_SIZE 3
#define MAX_SIZE 10
#define MOVE_INVALID 0
#define MOVE_OK 1
#define MOVE_MATCHED 2
#define PROCESS_PRINT_LIMIT 200
#define INPUT_EOF (-2000000)
#define EMPTY_LINE (-1000000)

/* --- Data structures --- */

typedef struct {
    int row;
    int col;
} Position;

typedef struct {
    int row;
    int col;
    int oldValue;
    int newValue;
} CellChange;

typedef struct MoveNode {
    CellChange *changes;
    int count;
    struct MoveNode *prev;
    struct MoveNode *next;
} MoveNode;

typedef struct {
    MoveNode *head;
    MoveNode *current;
    int undoCount;
} MoveHistory;

typedef struct {
    int **work;
    int **initial;
    int size;
    int sourceType;
    char userName[MAX_NAME_LENGTH];
    MoveHistory history;
} GameSession;

typedef struct {
    int **grid;
    int size;
    int n;
    Position *startPt;
    Position *endPt;
    int *order;
    long iterations;
    long iterationCap;
    Position *traceBuf;
    long backtracks;
    int printed;
} SolverContext;

typedef struct {
    char name[MAX_NAME_LENGTH];
    double score;
    double timeSeconds;
    int mode;
    int matrixSize;
    int sourceType;
    int undoCount;
    int gamesPlayed;
} ScoreRecord;

/* --- Function prototypes --- */

int readLine(char *buf, int size);
int readInt(void);
int readTwoInts(int *a, int *b);

int **createMatrix(int size);
void freeMatrix(int **matrix, int size);
int **copyMatrix(int **source, int size);
void printMatrix(int **matrix, int size);

int sameColorDegree(int **matrix, int size, int r, int c, int color);
int colorNeighborsExcept(int **matrix, int size, int r, int c, int color, int ex1r, int ex1c, int ex2r, int ex2c);
int countEmpty(int **matrix, int size);
int findEndpoints(int **matrix, int size, Position *start, Position *end);
int reachable(int **grid, int size, Position a, Position b);
int endpointsConnected(int **matrix, int size, Position a, Position b, int color);
int colorIsForestPath(int **matrix, int size, int color);
int colorIsSinglePath(int **matrix, int size, int color);
int verifySolution(int **matrix, int size);

void shuffleColors(int *perm, int size);
void shuffleInts(int *arr, int count);
int emptyNeighborCount(int **grid, int size, int r, int c, int exceptR, int exceptC);
int buildColorPartition(int **grid, int size, Position *sp, Position *ep);
void placeRandomMatrix(int **matrix, int size);
int **loadMatrixFromFile(const char *fileName, int *sizeOut);

void initHistory(MoveHistory *history);
void freeHistory(MoveHistory *history);
void pushMove(MoveHistory *history, CellChange *changes, int count);
int undoMove(int **matrix, MoveHistory *history);
int redoMove(int **matrix, MoveHistory *history);

int makeManualMove(int **matrix, int size, Position src, Position dst, MoveHistory *history);

void tracePath(int **grid, int size, int color, Position start, Position end, Position *outPath, int *lengthOut);
void printColorPath(SolverContext *ctx, int color);
void logConnect(SolverContext *ctx, int color);
void logTry(SolverContext *ctx, int color, int r, int c);
void logUndo(SolverContext *ctx, int color, int r, int c);
int dfsRoute(SolverContext *ctx, int color, int curR, int curC, int colorIndex);
int solveFrom(SolverContext *ctx, int colorIndex);
int solveAuto(int **matrix, int size, long *backtracksOut);

double computeScore(double timeSeconds, int mode, int matrixSize, int sourceType, int undoCount, int gamesPlayed);
int countUserGames(const char *fileName, const char *userName);
int saveScore(const char *fileName, ScoreRecord *record);
void showScores(const char *fileName);

int saveGame(const char *fileName, GameSession *session);
int loadGame(const char *fileName, GameSession *session);

void sanitizeName(char *text);
void recordCompletedGame(const char *userName, double timeSeconds, int mode, int matrixSize, int sourceType, int undoCount);
void playManual(GameSession *session);
void playAuto(GameSession *session);
void gameMenu(GameSession *session);
void resumeGame(void);
void mainMenu(void);

/* --- Input helpers --- */

/* Reads a line of text from standard input (at most size-1 characters) and removes the trailing
   newline. Inputs: buf (target buffer), size (buffer size).
   Output: returns TRUE if a line was read, or FALSE at end of input (buf is set to ""). */
int readLine(char *buf, int size){
    size_t length;
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return FALSE;
    }
    length = strlen(buf);
    while (length > 0 && (buf[length - 1] == '\n' || buf[length - 1] == '\r')) {
        buf[length - 1] = '\0';
        length--;
    }
    return TRUE;
}

/* Reads one integer from standard input through a text line. Inputs: none.
   Output: returns the integer, EMPTY_LINE if the line was empty, or INPUT_EOF at end of input. */
int readInt(void){
    char line[64];
    if (readLine(line, (int)sizeof(line)) == FALSE) {
        return INPUT_EOF;
    }
    if (line[0] == '\0') {
        return EMPTY_LINE;
    }
    return atoi(line);
}

/* Reads two integers (row and column) from a single text line. Inputs: pointers a and b.
   Output: returns TRUE if two integers were parsed, otherwise FALSE. */
int readTwoInts(int *a, int *b){
    char line[128];
    int got;
    readLine(line, (int)sizeof(line));
    got = sscanf(line, "%d %d", a, b);
    return (got == 2) ? TRUE : FALSE;
}

/* --- Matrix helpers --- */

/* Allocates a size x size integer matrix and sets every cell to EMPTY.
   Inputs: size. Output: returns a pointer to the new matrix. */
int **createMatrix(int size){
    int **matrix;
    int i, j;
    matrix = (int **)malloc((size_t)size * sizeof(int *));
    for (i = 0; i < size; i++) {
        matrix[i] = (int *)malloc((size_t)size * sizeof(int));
        for (j = 0; j < size; j++) {
            matrix[i][j] = EMPTY;
        }
    }
    return matrix;
}

/* Frees a previously allocated size x size matrix. Inputs: matrix, size. Output: none. */
void freeMatrix(int **matrix, int size){
    int i;
    if (matrix == NULL) {
        return;
    }
    for (i = 0; i < size; i++) {
        free(matrix[i]);
    }
    free(matrix);
}

/* Creates a deep copy of a size x size matrix. Inputs: source matrix, size.
   Output: returns a pointer to the newly allocated copy. */
int **copyMatrix(int **source, int size){
    int **matrix;
    int i, j;
    matrix = createMatrix(size);
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            matrix[i][j] = source[i][j];
        }
    }
    return matrix;
}

/* Prints the matrix with row and column indices; empty cells are shown as a dot.
   Inputs: matrix, size. Output: none (prints to the screen). */
void printMatrix(int **matrix, int size){
    int i, j;
    printf("\n      ");
    for (j = 0; j < size; j++) {
        printf("%3d", j);
    }
    printf("\n     +");
    for (j = 0; j < size; j++) {
        printf("---");
    }
    printf("\n");
    for (i = 0; i < size; i++) {
        printf("%4d |", i);
        for (j = 0; j < size; j++) {
            if (matrix[i][j] == EMPTY) {
                printf("  .");
            } else {
                printf("%3d", matrix[i][j]);
            }
        }
        printf("\n");
    }
    printf("\n");
}

/* --- Graph / validation helpers --- */

/* Counts how many of the four neighbours of cell (r,c) hold the given colour.
   Inputs: matrix, size, cell coordinates, colour. Output: returns the neighbour count. */
int sameColorDegree(int **matrix, int size, int r, int c, int color){
    int dr[4], dc[4];
    int d;
    int count;
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    count = 0;
    for (d = 0; d < 4; d++) {
        int rr, cc;
        rr = r + dr[d];
        cc = c + dc[d];
        if (rr >= 0 && rr < size && cc >= 0 && cc < size && matrix[rr][cc] == color) {
            count++;
        }
    }
    return count;
}

/* Counts neighbours of (r,c) equal to colour, excluding up to two given cells. Used by the solver
   to forbid a path from touching itself. Inputs: matrix, size, cell, colour, two excluded cells.
   Output: returns the count of qualifying same-colour neighbours. */
int colorNeighborsExcept(int **matrix, int size, int r, int c, int color, int ex1r, int ex1c, int ex2r, int ex2c){
    int dr[4], dc[4];
    int d;
    int count;
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    count = 0;
    for (d = 0; d < 4; d++) {
        int rr, cc;
        rr = r + dr[d];
        cc = c + dc[d];
        if (rr >= 0 && rr < size && cc >= 0 && cc < size && matrix[rr][cc] == color) {
            if (!(rr == ex1r && cc == ex1c) && !(rr == ex2r && cc == ex2c)) {
                count++;
            }
        }
    }
    return count;
}

/* Counts the number of empty cells in the matrix. Inputs: matrix, size. Output: returns the count. */
int countEmpty(int **matrix, int size){
    int i, j;
    int count;
    count = 0;
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            if (matrix[i][j] == EMPTY) {
                count++;
            }
        }
    }
    return count;
}

/* Records the two positions of each colour 1..size into start[] and end[].
   Inputs: matrix, size, pre-allocated start[] and end[] of length size+1.
   Output: returns TRUE if every colour appears exactly twice, otherwise FALSE. */
int findEndpoints(int **matrix, int size, Position *start, Position *end){
    int *count;
    int i, j, v;
    int ok;
    count = (int *)malloc((size_t)(size + 1) * sizeof(int));
    for (i = 0; i <= size; i++) {
        count[i] = 0;
    }
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            v = matrix[i][j];
            if (v >= 1 && v <= size) {
                if (count[v] == 0) {
                    start[v].row = i;
                    start[v].col = j;
                } else if (count[v] == 1) {
                    end[v].row = i;
                    end[v].col = j;
                }
                count[v]++;
            }
        }
    }
    ok = TRUE;
    for (v = 1; v <= size; v++) {
        if (count[v] != 2) {
            ok = FALSE;
        }
    }
    free(count);
    return ok;
}

/* Tests whether cell b can be reached from cell a moving only through empty cells (a and b
   themselves are treated as passable). Used as a solver pruning test.
   Inputs: grid, size, positions a and b. Output: returns TRUE if b is reachable. */
int reachable(int **grid, int size, Position a, Position b){
    int **visited;
    Position *queue;
    int head;
    int tail;
    int found;
    int dr[4], dc[4];
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    if (a.row == b.row && a.col == b.col) {
        return TRUE;
    }
    visited = createMatrix(size);
    queue = (Position *)malloc((size_t)(size * size) * sizeof(Position));
    head = 0;
    tail = 0;
    queue[tail] = a;
    tail++;
    visited[a.row][a.col] = 1;
    found = FALSE;
    while (head < tail && found == FALSE) {
        Position cur;
        int d;
        cur = queue[head];
        head++;
        for (d = 0; d < 4; d++) {
            int rr, cc;
            rr = cur.row + dr[d];
            cc = cur.col + dc[d];
            if (rr >= 0 && rr < size && cc >= 0 && cc < size && visited[rr][cc] == 0) {
                if (rr == b.row && cc == b.col) {
                    found = TRUE;
                    visited[rr][cc] = 1;
                } else if (grid[rr][cc] == EMPTY) {
                    visited[rr][cc] = 1;
                    queue[tail].row = rr;
                    queue[tail].col = cc;
                    tail++;
                }
            }
        }
    }
    free(queue);
    freeMatrix(visited, size);
    return found;
}

/* Tests whether two same-colour cells are connected through cells of that colour.
   Inputs: matrix, size, endpoint positions a and b, colour. Output: returns TRUE if connected. */
int endpointsConnected(int **matrix, int size, Position a, Position b, int color){
    int **visited;
    Position *queue;
    int head;
    int tail;
    int found;
    int dr[4], dc[4];
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    if (a.row == b.row && a.col == b.col) {
        return TRUE;
    }
    visited = createMatrix(size);
    queue = (Position *)malloc((size_t)(size * size) * sizeof(Position));
    head = 0;
    tail = 0;
    queue[tail] = a;
    tail++;
    visited[a.row][a.col] = 1;
    found = FALSE;
    while (head < tail && found == FALSE) {
        Position cur;
        int d;
        cur = queue[head];
        head++;
        for (d = 0; d < 4; d++) {
            int rr, cc;
            rr = cur.row + dr[d];
            cc = cur.col + dc[d];
            if (rr >= 0 && rr < size && cc >= 0 && cc < size && visited[rr][cc] == 0 && matrix[rr][cc] == color) {
                if (rr == b.row && cc == b.col) {
                    found = TRUE;
                }
                visited[rr][cc] = 1;
                queue[tail].row = rr;
                queue[tail].col = cc;
                tail++;
            }
        }
    }
    free(queue);
    freeMatrix(visited, size);
    return found;
}

/* Tests whether all cells of a colour form disjoint simple paths: no cell has more than two
   same-colour neighbours and the colour sub-graph contains no cycle.
   Inputs: matrix, size, colour. Output: returns TRUE if the colour is a valid path / forest. */
int colorIsForestPath(int **matrix, int size, int color){
    int **visited;
    Position *queue;
    int nodes;
    int edges;
    int components;
    int maxDegree;
    int i, j;
    int dr[4], dc[4];
    int ok;
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    visited = createMatrix(size);
    queue = (Position *)malloc((size_t)(size * size) * sizeof(Position));
    nodes = 0;
    edges = 0;
    components = 0;
    maxDegree = 0;
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            if (matrix[i][j] == color) {
                int degree;
                degree = sameColorDegree(matrix, size, i, j, color);
                if (degree > maxDegree) {
                    maxDegree = degree;
                }
                nodes++;
                if (j + 1 < size && matrix[i][j + 1] == color) {
                    edges++;
                }
                if (i + 1 < size && matrix[i + 1][j] == color) {
                    edges++;
                }
            }
        }
    }
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            if (matrix[i][j] == color && visited[i][j] == 0) {
                int qh, qt;
                components++;
                qh = 0;
                qt = 0;
                queue[qt].row = i;
                queue[qt].col = j;
                qt++;
                visited[i][j] = 1;
                while (qh < qt) {
                    Position cur;
                    int d;
                    cur = queue[qh];
                    qh++;
                    for (d = 0; d < 4; d++) {
                        int rr, cc;
                        rr = cur.row + dr[d];
                        cc = cur.col + dc[d];
                        if (rr >= 0 && rr < size && cc >= 0 && cc < size && matrix[rr][cc] == color && visited[rr][cc] == 0) {
                            visited[rr][cc] = 1;
                            queue[qt].row = rr;
                            queue[qt].col = cc;
                            qt++;
                        }
                    }
                }
            }
        }
    }
    ok = (maxDegree <= 2 && edges == nodes - components) ? TRUE : FALSE;
    free(queue);
    freeMatrix(visited, size);
    return ok;
}

/* Tests whether all cells of a colour form exactly one simple path: the colour is present (at least
   two cells), every cell has at most two same-colour neighbours, the cells are all connected (one
   component) and contain no cycle. Such a path automatically links the colour's two endpoints, which
   are its two degree-one tips. Unlike colorIsForestPath this also requires a single component, so it
   is used to confirm a colour is fully and correctly connected in a finished board.
   Inputs: matrix, size, colour. Output: returns TRUE if the colour is a single simple path. */
int colorIsSinglePath(int **matrix, int size, int color){
    int **visited;
    Position *queue;
    int nodes, edges;
    int components;
    int maxDegree;
    int i, j;
    int dr[4], dc[4];
    int ok;
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    visited = createMatrix(size);
    queue = (Position *)malloc((size_t)(size * size) * sizeof(Position));
    nodes = 0;
    edges = 0;
    components = 0;
    maxDegree = 0;
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            if (matrix[i][j] == color) {
                int degree;
                degree = sameColorDegree(matrix, size, i, j, color);
                if (degree > maxDegree) {
                    maxDegree = degree;
                }
                nodes++;
                if (j + 1 < size && matrix[i][j + 1] == color) {
                    edges++;
                }
                if (i + 1 < size && matrix[i + 1][j] == color) {
                    edges++;
                }
            }
        }
    }
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            if (matrix[i][j] == color && visited[i][j] == 0) {
                int qh, qt;
                components++;
                qh = 0;
                qt = 0;
                queue[qt].row = i;
                queue[qt].col = j;
                qt++;
                visited[i][j] = 1;
                while (qh < qt) {
                    Position cur;
                    int d;
                    cur = queue[qh];
                    qh++;
                    for (d = 0; d < 4; d++) {
                        int rr, cc;
                        rr = cur.row + dr[d];
                        cc = cur.col + dc[d];
                        if (rr >= 0 && rr < size && cc >= 0 && cc < size && matrix[rr][cc] == color && visited[rr][cc] == 0) {
                            visited[rr][cc] = 1;
                            queue[qt].row = rr;
                            queue[qt].col = cc;
                            qt++;
                        }
                    }
                }
            }
        }
    }
    ok = (nodes >= 2 && maxDegree <= 2 && components == 1 && edges == nodes - 1) ? TRUE : FALSE;
    free(queue);
    freeMatrix(visited, size);
    return ok;
}

/* Verifies a completed solution: no empty cell is left and every colour 1..size forms a single
   simple path (which connects that colour's two endpoints). Inputs: matrix, size.
   Output: returns TRUE if the whole board is correctly solved. */
int verifySolution(int **matrix, int size){
    int v;
    int ok;
    if (countEmpty(matrix, size) != 0) {
        return FALSE;
    }
    ok = TRUE;
    for (v = 1; v <= size && ok == TRUE; v++) {
        if (colorIsSinglePath(matrix, size, v) == FALSE) {
            ok = FALSE;
        }
    }
    return ok;
}

/* --- Board generation --- */

/* Builds a random permutation of the colours 1..size into perm[] using the Fisher-Yates shuffle, so
   that the colour assigned to each solution line is random.
   Inputs: pre-allocated perm[] of length size, size. Output: none. */
void shuffleColors(int *perm, int size){
    int i;
    for (i = 0; i < size; i++) {
        perm[i] = i + 1;
    }
    for (i = size - 1; i > 0; i--) {
        int k, tmp;
        k = rand() % (i + 1);
        tmp = perm[i];
        perm[i] = perm[k];
        perm[k] = tmp;
    }
}

/* Shuffles an integer array in place with the Fisher-Yates algorithm. Used to try the four step
   directions in a random order. Inputs: arr, count. Output: none. */
void shuffleInts(int *arr, int count){
    int i;
    for (i = count - 1; i > 0; i--) {
        int k;
        int tmp;
        k = rand() % (i + 1);
        tmp = arr[i];
        arr[i] = arr[k];
        arr[k] = tmp;
    }
}

/* Counts how many still-empty cells are orthogonally adjacent to (r,c), ignoring the cell
   (exceptR,exceptC). The path grower uses this as a look-ahead so it can prefer the most
   constrained next cell and avoid stranding empty cells. Inputs: grid, size, the cell, the cell to
   ignore. Output: the count. */
int emptyNeighborCount(int **grid, int size, int r, int c, int exceptR, int exceptC){
    int dr[4], dc[4];
    int d, cnt;
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    cnt = 0;
    for (d = 0; d < 4; d++) {
        int nr, nc;
        nr = r + dr[d];
        nc = c + dc[d];
        if (nr >= 0 && nr < size && nc >= 0 && nc < size && grid[nr][nc] == EMPTY) {
            if (!(nr == exceptR && nc == exceptC)) {
                cnt++;
            }
        }
    }
    return cnt;
}

/* Tries once to partition the whole board into size non-self-touching paths (each at least two
   cells) that together leave no empty cell. Colours are grown one after another, each starting from
   the first empty cell in scan order; a step extends the path into a still-empty neighbour that
   touches the path only at the current head (so a path never runs beside itself, as the matching
   rules require), preferring the neighbour with the fewest onward empty cells so tight gaps get
   filled first and cells are not stranded. Path lengths are kept close to remaining/coloursLeft so
   the colours share the board evenly. On success the two ends of every colour are written to sp[]
   and ep[]. Inputs: grid (work matrix), size, sp/ep arrays of length size+1.
   Output: returns TRUE when a complete, verified partition was built (grid then holds a solution). */
int buildColorPartition(int **grid, int size, Position *sp, Position *ep){
    int color;
    int i, j;
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            grid[i][j] = EMPTY;
        }
    }
    for (color = 1; color <= size; color++) {
        int remaining;
        int colorsLeft;
        int maxLen;
        int target;
        int len;
        int hr, hc, fr, fc;
        int keepGrowing;
        remaining = 0;
        colorsLeft = size - color + 1;
        for (i = 0; i < size; i++) {
            for (j = 0; j < size; j++) {
                if (grid[i][j] == EMPTY) {
                    remaining++;
                }
            }
        }
        maxLen = remaining - 2 * (colorsLeft - 1);
        if (maxLen < 2) {
            return FALSE;
        }
        target = remaining / colorsLeft;
        if (target < 2) {
            target = 2;
        }
        if (target > maxLen) {
            target = maxLen;
        }
        /* allow the last colour to take everything that is left */
        if (color == size) {
            target = maxLen;
        }
        hr = -1;
        hc = -1;
        for (i = 0; i < size && hr < 0; i++) {
            for (j = 0; j < size && hr < 0; j++) {
                if (grid[i][j] == EMPTY) {
                    hr = i;
                    hc = j;
                }
            }
        }
        fr = hr;
        fc = hc;
        grid[hr][hc] = color;
        len = 1;
        keepGrowing = TRUE;
        while (len < target && keepGrowing == TRUE) {
            int dr[4], dc[4];
            int d;
            int bestN;
            int br, bc;
            int ties;
            dr[0] = -1; 
            dr[1] = 1; 
            dr[2] = 0; 
            dr[3] = 0;
            dc[0] = 0; 
            dc[1] = 0; 
            dc[2] = -1; 
            dc[3] = 1;
            bestN = 99;
            br = -1;
            bc = -1;
            ties = 0;
            for (d = 0; d < 4; d++) {
                int nr, nc;
                nr = hr + dr[d];
                nc = hc + dc[d];
                if (nr >= 0 && nr < size && nc >= 0 && nc < size && grid[nr][nc] == EMPTY) {
                    if (colorNeighborsExcept(grid, size, nr, nc, color, hr, hc, -1, -1) == 0) {
                        int e;
                        e = emptyNeighborCount(grid, size, nr, nc, hr, hc);
                        if (e < bestN) {
                            bestN = e;
                            br = nr;
                            bc = nc;
                            ties = 1;
                        } else if (e == bestN) {
                            ties++;
                            if (rand() % ties == 0) {
                                br = nr;
                                bc = nc;
                            }
                        }
                    }
                }
            }
            if (br < 0) {
                keepGrowing = FALSE;
            } else {
                grid[br][bc] = color;
                hr = br;
                hc = bc;
                len++;
            }
        }
        if (len < 2) {
            return FALSE;
        }
        sp[color].row = fr;
        sp[color].col = fc;
        ep[color].row = hr;
        ep[color].col = hc;
    }
    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            if (grid[i][j] == EMPTY) {
                return FALSE;
            }
        }
    }
    return verifySolution(grid, size);
}

/* Generates a guaranteed-solvable random board with naturally scattered endpoints. It repeatedly
   tries to build a full partition of the board into size non-self-touching paths (see
   buildColorPartition); as soon as one succeeds, only the two ends of each path are kept as that
   colour's endpoints and the rest of the board is cleared for the player or the automatic solver to
   fill. Because the kept endpoints come from a real, verified non-crossing solution, the board can
   always be completed with no empty cells. The endpoints land at varied interior positions in a
   random colour order instead of being lined up along the edges. If every attempt fails (which can
   happen on the largest boards), a simple straight-line layout with a random orientation is used as
   a guaranteed fallback. Inputs: matrix (already allocated), size. Output: none. */
void placeRandomMatrix(int **matrix, int size){
    Position *sp;
    Position *ep;
    int **work;
    int attempt;
    int done;
    int i, j;
    sp = (Position *)malloc((size_t)(size + 1) * sizeof(Position));
    ep = (Position *)malloc((size_t)(size + 1) * sizeof(Position));
    work = createMatrix(size);
    done = FALSE;
    for (attempt = 0; attempt < 400 && done == FALSE; attempt++) {
        if (buildColorPartition(work, size, sp, ep) == TRUE) {
            int c;
            for (i = 0; i < size; i++) {
                for (j = 0; j < size; j++) {
                    matrix[i][j] = EMPTY;
                }
            }
            for (c = 1; c <= size; c++) {
                matrix[sp[c].row][sp[c].col] = c;
                matrix[ep[c].row][ep[c].col] = c;
            }
            done = TRUE;
        }
    }
    if (done == FALSE) {
        int *perm;
        int byColumn;
        perm = (int *)malloc((size_t)size * sizeof(int));
        shuffleColors(perm, size);
        byColumn = rand() % 2;
        for (i = 0; i < size; i++) {
            for (j = 0; j < size; j++) {
                matrix[i][j] = EMPTY;
            }
        }
        for (i = 0; i < size; i++) {
            if (byColumn == 1) {
                matrix[0][i] = perm[i];
                matrix[size - 1][i] = perm[i];
            } else {
                matrix[i][0] = perm[i];
                matrix[i][size - 1] = perm[i];
            }
        }
        free(perm);
    }
    freeMatrix(work, size);
    free(sp);
    free(ep);
}

/* Loads a board from a text file. Format: the first integer is the size N, then N*N integers given
   row by row (0 or a negative value means an empty cell).
   Inputs: file name, pointer sizeOut. Output: returns a new matrix and sets *sizeOut, or NULL if
   the file is missing or malformed. */
int **loadMatrixFromFile(const char *fileName, int *sizeOut){
    FILE *fp;
    int n, i, j;
    int **matrix;
    int readOk;
    fp = fopen(fileName, "r");
    if (fp == NULL) {
        return NULL;
    }
    if (fscanf(fp, "%d", &n) != 1 || n < MIN_SIZE || n > MAX_SIZE) {
        fclose(fp);
        return NULL;
    }
    matrix = createMatrix(n);
    readOk = TRUE;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            int value;
            if (fscanf(fp, "%d", &value) != 1) {
                readOk = FALSE;
                value = EMPTY;
            }
            if (value < 0) {
                value = EMPTY;
            }
            matrix[i][j] = value;
        }
    }
    fclose(fp);
    if (readOk == FALSE) {
        freeMatrix(matrix, n);
        return NULL;
    }
    *sizeOut = n;
    return matrix;
}

/* --- Move history (undo / redo) --- */

/* Initialises an empty move history. Inputs: pointer to a MoveHistory. Output: none. */
void initHistory(MoveHistory *history){
    history->head = NULL;
    history->current = NULL;
    history->undoCount = 0;
}

/* Frees every move node (and its change array) in the history. Inputs: history. Output: none. */
void freeHistory(MoveHistory *history){
    MoveNode *node;
    MoveNode *nextNode;
    node = history->head;
    while (node != NULL) {
        nextNode = node->next;
        free(node->changes);
        free(node);
        node = nextNode;
    }
    history->head = NULL;
    history->current = NULL;
}

/* Adds a new move after the current position, discarding any moves available for redo. The history
   takes ownership of the changes array. Inputs: history, changes array, change count. Output: none. */
void pushMove(MoveHistory *history, CellChange *changes, int count){
    MoveNode *node;
    MoveNode *toFree;
    MoveNode *nextFree;
    if (history->current == NULL) {
        toFree = history->head;
        while (toFree != NULL) {
            nextFree = toFree->next;
            free(toFree->changes);
            free(toFree);
            toFree = nextFree;
        }
        history->head = NULL;
    } else {
        toFree = history->current->next;
        while (toFree != NULL) {
            nextFree = toFree->next;
            free(toFree->changes);
            free(toFree);
            toFree = nextFree;
        }
        history->current->next = NULL;
    }
    node = (MoveNode *)malloc(sizeof(MoveNode));
    node->changes = changes;
    node->count = count;
    node->next = NULL;
    node->prev = history->current;
    if (history->current == NULL) {
        history->head = node;
    } else {
        history->current->next = node;
    }
    history->current = node;
}

/* Undoes the current move, restoring the affected cells, and moves the pointer back one step.
   Inputs: matrix, history. Output: returns TRUE if a move was undone. */
int undoMove(int **matrix, MoveHistory *history){
    int k;
    if (history->current == NULL) {
        return FALSE;
    }
    for (k = 0; k < history->current->count; k++) {
        CellChange ch;
        ch = history->current->changes[k];
        matrix[ch.row][ch.col] = ch.oldValue;
    }
    history->current = history->current->prev;
    history->undoCount++;
    return TRUE;
}

/* Redoes the next available move, re-applying its changes, and advances the pointer.
   Inputs: matrix, history. Output: returns TRUE if a move was redone. */
int redoMove(int **matrix, MoveHistory *history){
    MoveNode *target;
    int k;
    if (history->current == NULL) {
        target = history->head;
    } else {
        target = history->current->next;
    }
    if (target == NULL) {
        return FALSE;
    }
    for (k = 0; k < target->count; k++) {
        CellChange ch;
        ch = target->changes[k];
        matrix[ch.row][ch.col] = ch.newValue;
    }
    history->current = target;
    return TRUE;
}

/* --- Manual move --- */

/* Attempts a manual matching move. Starting from the colour at the source cell, it fills the empty
   cells along the straight line toward the destination (which must share the source's row or
   column). The move must start from a tip of the colour's current path and may not create a branch
   or a loop. Inputs: matrix, size, source position, destination position, history.
   Output: returns MOVE_INVALID, MOVE_OK or MOVE_MATCHED. */
int makeManualMove(int **matrix, int size, Position src, Position dst, MoveHistory *history){
    int color;
    int stepR, stepC;
    int distance;
    int blocked;
    int i;
    CellChange *changes;
    int changeCount;
    int matched;
    int valid;

    if (src.row < 0 || src.row >= size || src.col < 0 || src.col >= size) {
        return MOVE_INVALID;
    }
    if (dst.row < 0 || dst.row >= size || dst.col < 0 || dst.col >= size) {
        return MOVE_INVALID;
    }
    if (src.row == dst.row && src.col == dst.col) {
        return MOVE_INVALID;
    }
    if (src.row != dst.row && src.col != dst.col) {
        return MOVE_INVALID;
    }
    color = matrix[src.row][src.col];
    if (color < 1 || color > size) {
        return MOVE_INVALID;
    }
    if (sameColorDegree(matrix, size, src.row, src.col, color) >= 2) {
        return MOVE_INVALID;
    }
    if (colorIsSinglePath(matrix, size, color) == TRUE) {
        /* the colour is already a complete path between its two endpoints,
           so it must not be extended or altered any further */
        return MOVE_INVALID;
    }

    stepR = 0;
    stepC = 0;
    if (dst.row > src.row) {
        stepR = 1;
    } else if (dst.row < src.row) {
        stepR = -1;
    }
    if (dst.col > src.col) {
        stepC = 1;
    } else if (dst.col < src.col) {
        stepC = -1;
    }
    distance = (stepR != 0) ? (dst.row - src.row) : (dst.col - src.col);
    if (distance < 0) {
        distance = -distance;
    }

    blocked = FALSE;
    for (i = 1; i <= distance && blocked == FALSE; i++) {
        int cellR, cellC;
        cellR = src.row + stepR * i;
        cellC = src.col + stepC * i;
        if (i < distance) {
            if (matrix[cellR][cellC] != EMPTY) {
                blocked = TRUE;
            }
        } else {
            if (matrix[cellR][cellC] == EMPTY) {
                /* the destination cell will be filled */
                blocked = FALSE;
            } else if (matrix[cellR][cellC] == color && sameColorDegree(matrix, size, cellR, cellC, color) <= 1) {
                /* connecting onto the matching tip is allowed */
                blocked = FALSE;
            } else {
                blocked = TRUE;
            }
        }
    }
    if (blocked == TRUE) {
        return MOVE_INVALID;
    }

    changes = (CellChange *)malloc((size_t)distance * sizeof(CellChange));
    changeCount = 0;
    for (i = 1; i <= distance; i++) {
        int cellR, cellC;
        cellR = src.row + stepR * i;
        cellC = src.col + stepC * i;
        if (matrix[cellR][cellC] == EMPTY) {
            changes[changeCount].row = cellR;
            changes[changeCount].col = cellC;
            changes[changeCount].oldValue = EMPTY;
            changes[changeCount].newValue = color;
            matrix[cellR][cellC] = color;
            changeCount++;
        }
    }

    valid = colorIsForestPath(matrix, size, color);
    if (valid == FALSE) {
        for (i = 0; i < changeCount; i++) {
            matrix[changes[i].row][changes[i].col] = changes[i].oldValue;
        }
        free(changes);
        return MOVE_INVALID;
    }
    if (changeCount == 0) {
        free(changes);
        return MOVE_INVALID;
    }

    pushMove(history, changes, changeCount);
    matched = colorIsSinglePath(matrix, size, color);
    return (matched == TRUE) ? MOVE_MATCHED : MOVE_OK;
}

/* --- Automatic solver --- */

/* Traces the path of a colour from its start endpoint to its end endpoint following adjacent
   same-colour cells, writing the ordered cells into outPath and the length into *lengthOut.
   Inputs: grid, size, colour, start and end positions, output buffer. Output: none. */
void tracePath(int **grid, int size, int color, Position start, Position end, Position *outPath, int *lengthOut){
    int length;
    int curR, curC;
    int prevR, prevC;
    int done;
    int dr[4], dc[4];
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    length = 0;
    curR = start.row;
    curC = start.col;
    prevR = -1;
    prevC = -1;
    done = FALSE;
    while (done == FALSE) {
        int foundNext;
        int nextR, nextC;
        int d;
        outPath[length].row = curR;
        outPath[length].col = curC;
        length++;
        if (curR == end.row && curC == end.col) {
            done = TRUE;
        } else {
            foundNext = FALSE;
            nextR = -1;
            nextC = -1;
            for (d = 0; d < 4 && foundNext == FALSE; d++) {
                int rr, cc;
                rr = curR + dr[d];
                cc = curC + dc[d];
                if (rr >= 0 && rr < size && cc >= 0 && cc < size && grid[rr][cc] == color && !(rr == prevR && cc == prevC)) {
                    nextR = rr;
                    nextC = cc;
                    foundNext = TRUE;
                }
            }
            if (foundNext == FALSE) {
                done = TRUE;
            } else {
                prevR = curR;
                prevC = curC;
                curR = nextR;
                curC = nextC;
            }
        }
        if (length >= size * size) {
            done = TRUE;
        }
    }
    *lengthOut = length;
}

/* Prints the resulting path of a colour as a sequence of coordinates.
   Inputs: solver context, colour. Output: none (prints to the screen). */
void printColorPath(SolverContext *ctx, int color){
    int length;
    int k;
    tracePath(ctx->grid, ctx->size, color, ctx->startPt[color], ctx->endPt[color], ctx->traceBuf, &length);
    printf("   Color %d: ", color);
    for (k = 0; k < length; k++) {
        printf("(%d,%d)", ctx->traceBuf[k].row, ctx->traceBuf[k].col);
        if (k < length - 1) {
            printf(" -> ");
        }
    }
    printf("\n");
}

/* Reports that a colour's head has just reached its partner endpoint and the search is moving on to
   the next colour (limited output). Inputs: solver context, colour. Output: none. */
void logConnect(SolverContext *ctx, int color){
    if (ctx->printed < PROCESS_PRINT_LIMIT) {
        printf(" [connect] color %d reached its partner; moving on to the next color.\n", color);
        printColorPath(ctx, color);
        ctx->printed++;
    }
}

/* Reports that the search has extended a colour's path into a new cell (limited output).
   Inputs: solver context, colour, target cell. Output: none. */
void logTry(SolverContext *ctx, int color, int r, int c){
    if (ctx->printed < PROCESS_PRINT_LIMIT) {
        printf(" [try]    color %d -> (%d,%d)\n", color, r, c);
        ctx->printed++;
    }
}

/* Reports that a cell the search had entered turned out to be a wrong move and is being undone, and
   counts that undo operation. This is the automatic-mode equivalent of an undo in manual play.
   Inputs: solver context, colour, the cell being vacated. Output: none. */
void logUndo(SolverContext *ctx, int color, int r, int c){
    ctx->backtracks++;
    if (ctx->printed < PROCESS_PRINT_LIMIT) {
        printf(" [undo]   color %d: (%d,%d) led to a dead end, undoing this move.\n", color, r, c);
        ctx->printed++;
    }
}

/* Extends the path of a colour from the current head toward its partner endpoint, trying every
   self-avoiding route through empty cells. When the head reaches a cell next to the endpoint the
   path is closed and the remaining colours are solved recursively.
   Inputs: solver context, colour, head coordinates, colour index in the solving order.
   Output: returns TRUE if a complete solution of the whole board is found. */
int dfsRoute(SolverContext *ctx, int color, int curR, int curC, int colorIndex){
    int dr[4], dc[4];
    int d;
    int adjEnd;
    int result;
    Position end;
    end = ctx->endPt[color];
    dr[0] = -1; 
    dr[1] = 1; 
    dr[2] = 0; 
    dr[3] = 0;
    dc[0] = 0; 
    dc[1] = 0; 
    dc[2] = -1; 
    dc[3] = 1;
    ctx->iterations++;
    if (ctx->iterations > ctx->iterationCap) {
        return FALSE;
    }
    adjEnd = FALSE;
    if (curR == end.row && (curC == end.col - 1 || curC == end.col + 1)) {
        adjEnd = TRUE;
    }
    if (curC == end.col && (curR == end.row - 1 || curR == end.row + 1)) {
        adjEnd = TRUE;
    }
    result = FALSE;
    if (adjEnd == TRUE) {
        logConnect(ctx, color);
        if (solveFrom(ctx, colorIndex + 1)) {
            result = TRUE;
        }
        return result;
    }
    for (d = 0; d < 4 && result == FALSE; d++) {
        int nr, nc;
        nr = curR + dr[d];
        nc = curC + dc[d];
        if (nr >= 0 && nr < ctx->size && nc >= 0 && nc < ctx->size && ctx->grid[nr][nc] == EMPTY) {
            if (colorNeighborsExcept(ctx->grid, ctx->size, nr, nc, color, curR, curC, end.row, end.col) == 0) {
                Position head;
                int kIndex;
                int reachOk;
                ctx->grid[nr][nc] = color;
                head.row = nr;
                head.col = nc;
                logTry(ctx, color, nr, nc);
                reachOk = reachable(ctx->grid, ctx->size, head, end);
                if (reachOk == TRUE) {
                    int allOk;
                    allOk = TRUE;
                    for (kIndex = colorIndex + 1; kIndex < ctx->n && allOk == TRUE; kIndex++) {
                        int other;
                        other = ctx->order[kIndex];
                        if (reachable(ctx->grid, ctx->size, ctx->startPt[other], ctx->endPt[other]) == FALSE) {
                            allOk = FALSE;
                        }
                    }
                    if (allOk == TRUE) {
                        if (dfsRoute(ctx, color, nr, nc, colorIndex)) {
                            result = TRUE;
                        }
                    }
                }
                if (result == FALSE) {
                    ctx->grid[nr][nc] = EMPTY;
                    logUndo(ctx, color, nr, nc);
                }
            }
        }
    }
    return result;
}

/* Solves all colours with index >= colorIndex (in the solving order) so that every cell is filled
   with non-crossing paths. Inputs: solver context, colour index. Output: returns TRUE on success. */
int solveFrom(SolverContext *ctx, int colorIndex){
    int color;
    if (colorIndex >= ctx->n) {
        return (countEmpty(ctx->grid, ctx->size) == 0) ? TRUE : FALSE;
    }
    color = ctx->order[colorIndex];
    return dfsRoute(ctx, color, ctx->startPt[color].row, ctx->startPt[color].col, colorIndex);
}

/* Runs the automatic solver on a board, printing the search process and the final solution.
   Inputs: matrix (filled in place), size, pointer backtracksOut for the number of undo operations.
   Output: returns TRUE if the board was solved. */
int solveAuto(int **matrix, int size, long *backtracksOut){
    SolverContext ctx;
    Position *start;
    Position *end;
    int *order;
    int i;
    int solved;
    start = (Position *)malloc((size_t)(size + 1) * sizeof(Position));
    end = (Position *)malloc((size_t)(size + 1) * sizeof(Position));
    if (!findEndpoints(matrix, size, start, end)) {
        printf("The board is not valid: every number must appear exactly twice.\n");
        free(start);
        free(end);
        *backtracksOut = 0;
        return FALSE;
    }
    order = (int *)malloc((size_t)size * sizeof(int));
    for (i = 0; i < size; i++) {
        order[i] = i + 1;
    }
    ctx.grid = matrix;
    ctx.size = size;
    ctx.n = size;
    ctx.startPt = start;
    ctx.endPt = end;
    ctx.order = order;
    ctx.iterations = 0;
    ctx.iterationCap = 20000000L;
    ctx.traceBuf = (Position *)malloc((size_t)(size * size) * sizeof(Position));
    ctx.backtracks = 0;
    ctx.printed = 0;
    printf("\nAutomatic solver started. It reports each move it tries and each wrong move it\n");
    printf("undoes, then prints the final paths and the solved board.\n\n");
    solved = solveFrom(&ctx, 0);
    if (ctx.printed >= PROCESS_PRINT_LIMIT) {
        printf(" ... (further search steps are hidden to keep the output short)\n");
    }
    if (solved == TRUE) {
        printf("\n=== SOLUTION FOUND ===\n");
        printf("Total wrong moves undone during the search: %ld\n", ctx.backtracks);
        printf("Final paths:\n");
        for (i = 1; i <= size; i++) {
            printColorPath(&ctx, i);
        }
        printf("\nResulting matrix:\n");
        printMatrix(matrix, size);
    } else {
        printf("\nThe automatic solver could not complete this board.\n");
    }
    *backtracksOut = ctx.backtracks;
    free(start);
    free(end);
    free(order);
    free(ctx.traceBuf);
    return solved;
}

/* --- Scoring and player performance --- */

/* Computes the score for a finished game. A larger matrix is worth more; manual mode and randomly
   generated boards earn bonuses; time taken and undo operations cost points; the number of games
   already played by the user adds a small experience bonus.
   Inputs: time in seconds, mode, matrix size, source type, undo count, games played.
   Output: returns the score (never negative). */
double computeScore(double timeSeconds, int mode, int matrixSize, int sourceType, int undoCount, int gamesPlayed){
    double base;
    double modeFactor;
    double sourceFactor;
    double timePenalty;
    double undoPenalty;
    double experienceBonus;
    double score;
    base = (double)(matrixSize * matrixSize) * 100.0;
    modeFactor = (mode == MODE_MANUAL) ? 1.5 : 1.0;
    sourceFactor = (sourceType == SOURCE_RANDOM) ? 1.2 : 1.0;
    timePenalty = timeSeconds * 2.0;
    undoPenalty = (double)undoCount * 25.0;
    experienceBonus = (double)gamesPlayed * 10.0;
    score = base * modeFactor * sourceFactor - timePenalty - undoPenalty + experienceBonus;
    if (score < 0.0) {
        score = 0.0;
    }
    return score;
}

/* Counts how many games the given user has already recorded in the score file.
   Inputs: file name, user name. Output: returns the count (0 if none / file missing). */
int countUserGames(const char *fileName, const char *userName){
    FILE *fp;
    char name[MAX_NAME_LENGTH];
    double score;
    double timeSeconds;
    int mode;
    int matrixSize;
    int sourceType;
    int undoCount;
    int games;
    int total;
    fp = fopen(fileName, "r");
    if (fp == NULL) {
        return 0;
    }
    total = 0;
    while (fscanf(fp, "%49s %lf %lf %d %d %d %d %d", name, &score, &timeSeconds, &mode, &matrixSize, &sourceType, &undoCount, &games) == 8) {
        if (strcmp(name, userName) == 0) {
            total++;
        }
    }
    fclose(fp);
    return total;
}

/* Appends one game record to the score file. Inputs: file name, record. Output: returns TRUE 
   on success. */
int saveScore(const char *fileName, ScoreRecord *record){
    FILE *fp;
    fp = fopen(fileName, "a");
    if (fp == NULL) {
        return FALSE;
    }
    fprintf(fp, "%s %.2f %.2f %d %d %d %d %d\n", record->name, record->score, record->timeSeconds, record->mode, record->matrixSize, record->sourceType, record->undoCount, record->gamesPlayed);
    fclose(fp);
    return TRUE;
}

/* Reads all game records, sorts them by score (highest first) and prints them as a table.
   Inputs: file name. Output: none (prints to the screen). */
void showScores(const char *fileName){
    FILE *fp;
    ScoreRecord *records;
    int count;
    int i, j;
    fp = fopen(fileName, "r");
    if (fp == NULL) {
        printf("\nNo scores have been recorded yet.\n");
        return;
    }
    records = (ScoreRecord *)malloc((size_t)MAX_RECORDS * sizeof(ScoreRecord));
    count = 0;
    while (count < MAX_RECORDS &&
           fscanf(fp, "%49s %lf %lf %d %d %d %d %d", records[count].name, &records[count].score, &records[count].timeSeconds, &records[count].mode, &records[count].matrixSize, &records[count].sourceType, &records[count].undoCount, &records[count].gamesPlayed) == 8) {
        count++;
    }
    fclose(fp);
    if (count == 0) {
        printf("\nNo scores have been recorded yet.\n");
        free(records);
        return;
    }
    for (i = 0; i < count - 1; i++) {
        int best;
        best = i;
        for (j = i + 1; j < count; j++) {
            if (records[j].score > records[best].score) {
                best = j;
            }
        }
        if (best != i) {
            ScoreRecord tmp;
            tmp = records[i];
            records[i] = records[best];
            records[best] = tmp;
        }
    }
    printf("\n======================= PLAYER SCORES =======================\n");
    printf("%-15s %8s %8s %7s %5s %7s %5s\n", "Player", "Score", "Time(s)", "Mode", "Size", "Source", "Undo");
    printf("-------------------------------------------------------------\n");
    for (i = 0; i < count; i++) {
        const char *modeText;
        const char *sourceText;
        modeText = (records[i].mode == MODE_MANUAL) ? "Manual" : "Auto";
        sourceText = (records[i].sourceType == SOURCE_RANDOM) ? "Random" : "File";
        printf("%-15s %8.2f %8.2f %7s %5d %7s %5d\n", records[i].name, records[i].score, records[i].timeSeconds, modeText, records[i].matrixSize, sourceText, records[i].undoCount);
    }
    printf("=============================================================\n");
    free(records);
}

/* --- Save / resume a game --- */

/* Saves the current game so it can be resumed later: matrix size, source type, user name, the
   initial endpoint matrix and the full move list with the current undo / redo position.
   Inputs: file name, session. Output: returns TRUE on success. */
int saveGame(const char *fileName, GameSession *session){
    FILE *fp;
    int i, j, k;
    int moveCount;
    int currentIndex;
    MoveNode *node;
    fp = fopen(fileName, "w");
    if (fp == NULL) {
        return FALSE;
    }
    fprintf(fp, "%d %d\n", session->size, session->sourceType);
    fprintf(fp, "%s\n", session->userName);
    for (i = 0; i < session->size; i++) {
        for (j = 0; j < session->size; j++) {
            fprintf(fp, "%d ", session->initial[i][j]);
        }
        fprintf(fp, "\n");
    }
    moveCount = 0;
    node = session->history.head;
    while (node != NULL) {
        moveCount++;
        node = node->next;
    }
    currentIndex = 0;
    if (session->history.current != NULL) {
        currentIndex = 1;
        node = session->history.head;
        while (node != session->history.current && node != NULL) {
            currentIndex++;
            node = node->next;
        }
    }
    fprintf(fp, "%d %d %d\n", moveCount, currentIndex, session->history.undoCount);
    node = session->history.head;
    while (node != NULL) {
        fprintf(fp, "%d\n", node->count);
        for (k = 0; k < node->count; k++) {
            fprintf(fp, "%d %d %d %d\n", node->changes[k].row, node->changes[k].col, node->changes[k].oldValue, node->changes[k].newValue);
        }
        node = node->next;
    }
    fclose(fp);
    return TRUE;
}

/* Loads a saved game and rebuilds the session: allocates the initial and working matrices, restores
   the move list and replays the applied moves so play can continue.
   Inputs: file name, pointer to a session to fill. Output: returns TRUE on success. */
int loadGame(const char *fileName, GameSession *session){
    FILE *fp;
    int i, j, k;
    int moveCount, undoCount;
    int currentIndex;
    int idx;
    MoveNode *prevNode;
    MoveNode *node;
    fp = fopen(fileName, "r");
    if (fp == NULL) {
        return FALSE;
    }
    if (fscanf(fp, "%d %d", &session->size, &session->sourceType) != 2) {
        fclose(fp);
        return FALSE;
    }
    if (fscanf(fp, "%49s", session->userName) != 1) {
        fclose(fp);
        return FALSE;
    }
    session->initial = createMatrix(session->size);
    for (i = 0; i < session->size; i++) {
        for (j = 0; j < session->size; j++) {
            int value;
            if (fscanf(fp, "%d", &value) != 1) {
                fclose(fp);
                freeMatrix(session->initial, session->size);
                return FALSE;
            }
            session->initial[i][j] = value;
        }
    }
    if (fscanf(fp, "%d %d %d", &moveCount, &currentIndex, &undoCount) != 3) {
        fclose(fp);
        freeMatrix(session->initial, session->size);
        return FALSE;
    }
    initHistory(&session->history);
    session->history.undoCount = undoCount;
    prevNode = NULL;
    for (i = 0; i < moveCount; i++) {
        int cnt;
        CellChange *changes;
        if (fscanf(fp, "%d", &cnt) != 1) {
            freeMatrix(session->initial, session->size);
            freeHistory(&session->history);
            fclose(fp);
            return FALSE;
        }
        changes = (CellChange *)malloc((size_t)cnt * sizeof(CellChange));
        for (k = 0; k < cnt; k++) {
            if (fscanf(fp, "%d %d %d %d", &changes[k].row, &changes[k].col, &changes[k].oldValue, &changes[k].newValue) != 4) {
                changes[k].row = 0;
                changes[k].col = 0;
                changes[k].oldValue = 0;
                changes[k].newValue = 0;
            }
        }
        node = (MoveNode *)malloc(sizeof(MoveNode));
        node->changes = changes;
        node->count = cnt;
        node->next = NULL;
        node->prev = prevNode;
        if (prevNode == NULL) {
            session->history.head = node;
        } else {
            prevNode->next = node;
        }
        prevNode = node;
    }
    fclose(fp);
    session->work = copyMatrix(session->initial, session->size);
    node = session->history.head;
    idx = 0;
    session->history.current = NULL;
    while (idx < currentIndex && node != NULL) {
        for (k = 0; k < node->count; k++) {
            session->work[node->changes[k].row][node->changes[k].col] = node->changes[k].newValue;
        }
        session->history.current = node;
        node = node->next;
        idx++;
    }
    return TRUE;
}

/* --- Gameplay --- */

/* Replaces any whitespace in a string with underscores so it stays a single token in the files; if
   the string is empty it is replaced by a default name. Inputs: text buffer. Output: none. */
void sanitizeName(char *text){
    int i;
    i = 0;
    while (text[i] != '\0') {
        if (text[i] == ' ' || text[i] == '\t') {
            text[i] = '_';
        }
        i++;
    }
    if (text[0] == '\0') {
        strcpy(text, "player");
    }
}

/* Records a completed game: works out the score, prints it and appends it to the score file.
   Inputs: user name, time in seconds, mode, matrix size, source type, undo count. Output: none. */
void recordCompletedGame(const char *userName, double timeSeconds, int mode, int matrixSize, int sourceType, int undoCount){
    ScoreRecord record;
    int gamesPlayed;
    gamesPlayed = countUserGames(SCORE_FILE, userName) + 1;
    strncpy(record.name, userName, MAX_NAME_LENGTH - 1);
    record.name[MAX_NAME_LENGTH - 1] = '\0';
    record.timeSeconds = timeSeconds;
    record.mode = mode;
    record.matrixSize = matrixSize;
    record.sourceType = sourceType;
    record.undoCount = undoCount;
    record.gamesPlayed = gamesPlayed;
    record.score = computeScore(timeSeconds, mode, matrixSize, sourceType, undoCount, gamesPlayed);
    printf("\nGame completed!\n");
    printf("Time: %.0f seconds | Undo operations: %d | Games played: %d\n", timeSeconds, undoCount, gamesPlayed);
    printf("Your score for this game: %.2f\n", record.score);
    saveScore(SCORE_FILE, &record);
}

/* Runs the interactive manual gameplay loop on the session's working matrix. The player can make
   moves, undo, redo, save or quit. The game ends when every number is matched and no empty cell is
   left. Inputs: session. Output: none. */
void playManual(GameSession *session){
    time_t startTime;
    int running;
    int finished;
    int step;
    startTime = time(NULL);
    running = TRUE;
    finished = FALSE;
    step = 0;
    while (running == TRUE) {
        int choice;
        printMatrix(session->work, session->size);
        if (verifySolution(session->work, session->size) == TRUE) {
            finished = TRUE;
            running = FALSE;
        } else {
            printf("Empty cells left: %d | Undo count: %d\n", countEmpty(session->work, session->size), session->history.undoCount);
            printf("Manual mode options:\n");
            printf("  1) Make a move (fill a straight line from a tip)\n");
            printf("  2) Undo\n");
            printf("  3) Redo\n");
            printf("  4) Save and exit to menu\n");
            printf("  5) Quit without saving\n");
            printf("Choice: ");
            choice = readInt();
            if (choice == INPUT_EOF) {
                running = FALSE;
            } else {
                switch (choice) {
                    case 1: {
                        Position src;
                        Position dst;
                        int okSrc, okDst;
                        printf("Enter source cell as 'row col': ");
                        okSrc = readTwoInts(&src.row, &src.col);
                        printf("Enter destination cell as 'row col': ");
                        okDst = readTwoInts(&dst.row, &dst.col);
                        if (okSrc == FALSE || okDst == FALSE) {
                            printf("Invalid input. Enter two numbers separated by a space.\n");
                        } else {
                            int status;
                            status = makeManualMove(session->work, session->size, src, dst, &session->history);
                            if (status == MOVE_INVALID) {
                                printf("Invalid move. Remember: straight line, empty cells in between, start from a tip of the path.\n");
                            } else {
                                step++;
                                printf("Step %d: Source: (%d,%d), Destination: (%d,%d)", step, src.row, src.col, dst.row, dst.col);
                                if (status == MOVE_MATCHED) {
                                    printf("   Numbers are matched");
                                }
                                printf("\n");
                            }
                        }
                        break;
                    }
                    case 2: {
                        if (undoMove(session->work, &session->history) == TRUE) {
                            printf("Step %d undone; the matrix is back to its previous state.\n", step);
                            if (step > 0) {
                                step--;
                            }
                        } else {
                            printf("There is no move to undo.\n");
                        }
                        break;
                    }
                    case 3: {
                        if (redoMove(session->work, &session->history) == TRUE) {
                            step++;
                            printf("Step %d redone.\n", step);
                        } else {
                            printf("There is no move to redo.\n");
                        }
                        break;
                    }
                    case 4: {
                        if (saveGame(SAVE_FILE, session) == TRUE) {
                            printf("Game saved. Resume it later from the main menu.\n");
                        } else {
                            printf("Could not save the game.\n");
                        }
                        running = FALSE;
                        break;
                    }
                    case 5: {
                        printf("Leaving the game without saving.\n");
                        running = FALSE;
                        break;
                    }
                    default: {
                        printf("Please choose a valid option.\n");
                        break;
                    }
                }
            }
        }
    }
    if (finished == TRUE) {
        double elapsed;
        elapsed = difftime(time(NULL), startTime);
        printf("\nAll numbers matched and the board is full. Well done!\n");
        printMatrix(session->work, session->size);
        recordCompletedGame(session->userName, elapsed, MODE_MANUAL, session->size, session->sourceType, session->history.undoCount);
    }
}

/* Runs the automatic gameplay on a fresh copy of the board, then records the resulting score.
   Inputs: session. Output: none. */
void playAuto(GameSession *session){
    time_t startTime;
    long backtracks;
    int solved;
    freeMatrix(session->work, session->size);
    session->work = copyMatrix(session->initial, session->size);
    freeHistory(&session->history);
    initHistory(&session->history);
    startTime = time(NULL);
    solved = solveAuto(session->work, session->size, &backtracks);
    if (solved == TRUE) {
        double elapsed;
        elapsed = difftime(time(NULL), startTime);
        recordCompletedGame(session->userName, elapsed, MODE_AUTO, session->size, session->sourceType, (int)backtracks);
    }
}

/* Runs the game menu for a session created from a random or file board. Asks for the user name,
   then lets the user play in manual or automatic mode repeatedly until they return to the main
   menu. Frees the session's matrices on exit. Inputs: session. Output: none. */
void gameMenu(GameSession *session){
    int running;
    char nameBuffer[MAX_NAME_LENGTH];
    printf("\nEnter your user name: ");
    readLine(nameBuffer, MAX_NAME_LENGTH);
    sanitizeName(nameBuffer);
    strncpy(session->userName, nameBuffer, MAX_NAME_LENGTH - 1);
    session->userName[MAX_NAME_LENGTH - 1] = '\0';
    initHistory(&session->history);
    session->work = copyMatrix(session->initial, session->size);
    running = TRUE;
    while (running == TRUE) {
        int choice;
        printf("\n--- Game Menu (player: %s) ---\n", session->userName);
        printf("  1) Play in Manual Mode\n");
        printf("  2) Play in Automatic Mode\n");
        printf("  3) Return to Main Menu\n");
        printf("Choice: ");
        choice = readInt();
        if (choice == INPUT_EOF) {
            running = FALSE;
        } else {
            switch (choice) {
                case 1: {
                    freeMatrix(session->work, session->size);
                    session->work = copyMatrix(session->initial, session->size);
                    freeHistory(&session->history);
                    initHistory(&session->history);
                    playManual(session);
                    break;
                }
                case 2: {
                    playAuto(session);
                    break;
                }
                case 3: {
                    running = FALSE;
                    break;
                }
                default: {
                    printf("Please choose a valid option.\n");
                    break;
                }
            }
        }
    }
    freeHistory(&session->history);
    freeMatrix(session->work, session->size);
    freeMatrix(session->initial, session->size);
}

/* Loads a previously saved game and continues it in manual mode. Inputs: none.
   Output: none (prints messages, frees the loaded session). */
void resumeGame(void){
    GameSession session;
    if (loadGame(SAVE_FILE, &session) == FALSE) {
        printf("\nNo saved game was found (or it could not be read).\n");
        return;
    }
    printf("\nResuming saved game for player: %s\n", session.userName);
    playManual(&session);
    freeHistory(&session.history);
    freeMatrix(session.work, session.size);
    freeMatrix(session.initial, session.size);
}

/* Shows the main menu and dispatches the user's choice until they exit. Inputs: none. Output: none. */
void mainMenu(void){
    int running;
    running = TRUE;
    while (running == TRUE) {
        int choice;
        printf("\n===== NUMBER MATCHING GAME - MAIN MENU =====\n");
        printf("  1) Create Random Matrix\n");
        printf("  2) Create Matrix from File\n");
        printf("  3) Show User Scores\n");
        printf("  4) Resume Saved Game\n");
        printf("  5) Exit\n");
        printf("Choice: ");
        choice = readInt();
        if (choice == INPUT_EOF) {
            running = FALSE;
        } else {
            switch (choice) {
                case 1: {
                    int size;
                    printf("Enter matrix size N (%d-%d): ", MIN_SIZE, MAX_SIZE);
                    size = readInt();
                    if (size < MIN_SIZE || size > MAX_SIZE) {
                        printf("Invalid size.\n");
                    } else {
                        GameSession session;
                        session.size = size;
                        session.sourceType = SOURCE_RANDOM;
                        session.initial = createMatrix(size);
                        placeRandomMatrix(session.initial, size);
                        printf("\nA random solvable board has been created:\n");
                        printMatrix(session.initial, size);
                        gameMenu(&session);
                    }
                    break;
                }
                case 2: {
                    char fileName[256];
                    int loadedSize;
                    int **loaded;
                    printf("Enter the input file name (for example input1.txt): ");
                    readLine(fileName, (int)sizeof(fileName));
                    loaded = loadMatrixFromFile(fileName, &loadedSize);
                    if (loaded == NULL) {
                        printf("Could not read a valid matrix from that file.\n");
                    } else {
                        Position *start;
                        Position *end;
                        int valid;
                        start = (Position *)malloc((size_t)(loadedSize + 1) * sizeof(Position));
                        end = (Position *)malloc((size_t)(loadedSize + 1) * sizeof(Position));
                        valid = findEndpoints(loaded, loadedSize, start, end);
                        free(start);
                        free(end);
                        if (valid == FALSE) {
                            printf("Invalid board: every number 1..N must appear exactly twice.\n");
                            freeMatrix(loaded, loadedSize);
                        } else {
                            GameSession session;
                            session.size = loadedSize;
                            session.sourceType = SOURCE_FILE;
                            session.initial = loaded;
                            printf("\nBoard loaded from file:\n");
                            printMatrix(session.initial, loadedSize);
                            gameMenu(&session);
                        }
                    }
                    break;
                }
                case 3: {
                    showScores(SCORE_FILE);
                    break;
                }
                case 4: {
                    resumeGame();
                    break;
                }
                case 5: {
                    printf("Goodbye!\n");
                    running = FALSE;
                    break;
                }
                default: {
                    printf("Please choose a valid option.\n");
                    break;
                }
            }
        }
    }
}

/* Program entry point. Seeds the random generator and runs the main menu. Output: returns 0. */
int main(void){
    srand((unsigned int)time(NULL));
    printf("Welcome to the Number Matching Game (N x N).\n");
    printf("Match identical numbers with non-crossing paths and fill the whole board.\n");
    mainMenu();
    return 0;
}