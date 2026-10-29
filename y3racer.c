#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "./follow_line/follow_line.h"

#define MAX_LINE_LENGTH 8192
#define BOOSTS_AT_START 5

typedef struct {
    int x;
    int y;
    int vx;
    int vy;
    int gas;
    int boosts;
} PilotState;

typedef struct {
    int width;
    int height;
    char **grid;
} Map;

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    int x, y, vx, vy, gas, first_ax, first_ay, score;
} BeamState;

Map* initialize_map(int width, int height) {
    int i;
    Map *map = malloc(sizeof(Map));
    if (map == NULL) {
        exit(EXIT_FAILURE);
    }
    map->width = width;
    map->height = height;
    map->grid = (char**)malloc(height * sizeof(char*));
    if (map->grid == NULL) {
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < height; i++) {
        map->grid[i] = (char*)malloc((width + 2) * sizeof(char));
        if (map->grid[i] == NULL) {
            exit(EXIT_FAILURE);
        }
    }
    return map;
}

void free_map(Map *map) {
    int i;
    if (map != NULL) {
        if (map->grid != NULL) {
            for (i = 0; i < map->height; i++) {
                free(map->grid[i]);
            }
            free(map->grid);
        }
        free(map);
    }
}

Map* read_map(int *initial_gas) {
    char line_buffer[MAX_LINE_LENGTH];
    int width, height, i;
    size_t len;
    Map *map;
    if (fgets(line_buffer, MAX_LINE_LENGTH, stdin) == NULL) {
        exit(EXIT_FAILURE);
    }
    sscanf(line_buffer, "%d %d %d", &width, &height, initial_gas);
    map = initialize_map(width, height);
    for (i = 0; i < height; i++) {
        if (fgets(line_buffer, MAX_LINE_LENGTH, stdin) != NULL) {
            strcpy(map->grid[i], line_buffer);
            len = strlen(map->grid[i]);
            if (len > 0 && map->grid[i][len - 1] == '\n') {
                map->grid[i][len - 1] = '\0';
            }
        }
    }
    return map;
}

/* la 'Heatmap' généré au 1er tour, va être utilisé dans tous les autres tours */ 
int** generate_heatmap(Map *map) {
    int width = map->width;
    int height = map->height;
    int i, j, x, y, nx, ny;
    int head = 0, tail = 0;
    int dx[] = {-1,  0,  1, -1, 1, -1, 0, 1};
    int dy[] = {-1, -1, -1,  0, 0,  1, 1, 1};
    int **heatmap;
    Point *queue;
    Point current;
    heatmap = (int**)malloc(height * sizeof(int*));
    for (i = 0; i < height; i++) {
        heatmap[i] = (int*)malloc(width * sizeof(int));
        for (j = 0; j < width; j++) {
            heatmap[i][j] = 1000000;
        }
    }
    queue = (Point*)malloc(width * height * sizeof(Point));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            if (map->grid[y][x] == '=') {
                heatmap[y][x] = 0;
                queue[tail].x = x;
                queue[tail].y = y;
                tail++;
            }
        }
    }
    while (head < tail) {
        current = queue[head];
        head++;
        for (i = 0; i < 8; i++) {
            nx = current.x + dx[i];
            ny = current.y + dy[i];
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                if (map->grid[ny][nx] != '.') {
                    if (heatmap[current.y][current.x] + 1 < heatmap[ny][nx]) {
                        heatmap[ny][nx] = heatmap[current.y][current.x] + 1;
                        queue[tail].x = nx;
                        queue[tail].y = ny;
                        tail++;
                    }
                }
            }
        }
    }
    free(queue);
    return heatmap;
}

void free_heatmap(int **heatmap, int height) {
    int i;
    if (heatmap != NULL) {
        for (i = 0; i < height; i++) {
            free(heatmap[i]);
        }
        free(heatmap);
    }
}

/* A partir d'une position donnée, on va chercher si un placement est permit ou
 * non (pour le GDC */
int is_move_valid(int current_x, int current_y, int speed_x, int speed_y, int acc_x, int acc_y, Map* map) {
    int future_speed_x = speed_x + acc_x;
    int future_speed_y = speed_y + acc_y;
    int target_x = current_x + future_speed_x;
    int target_y = current_y + future_speed_y;
    InfoLine segment;
    Pos2Dint inter_point;
    int status = 1;
    int in_sand = 0;
    int speed_sq = (future_speed_x * future_speed_x) + (future_speed_y * future_speed_y);
    if (target_x < 0 || target_x >= map->width || target_y < 0 || target_y >= map->height) {
        return 0;
    }
    if (map->grid[current_y][current_x] == '~') {
        in_sand = 1;
    }
    initLine(current_x, current_y, target_x, target_y, &segment);
    while (status == 1) {
        status = nextPoint(&segment, &inter_point, 1);
        if (inter_point.x < 0 || inter_point.x >= map->width || inter_point.y < 0 || inter_point.y >= map->height) {
            return 0;
        }
        if (map->grid[inter_point.y][inter_point.x] == '.') {
            return 0;
        }
        if (map->grid[inter_point.y][inter_point.x] == '~') {
            in_sand = 1;
        }
    }
    if (in_sand == 1 && speed_sq > 1) {
        return 0;
    }
    if (in_sand == 0 && speed_sq > 25) {
        return 0;
    }
    return 1;
}

/* on va faire le choix de notre prochain déplacement en tenant compte du
 * carburant, du sable... */
void get_best_action(PilotState *pilot, Map *map, int **heatmap, int *best_acc_x, int *best_acc_y) {
    int ax, ay, i, j, depth, in_sand, score_i, score_j;
    int current_count = 1;
    int next_count = 0;
    int min_score = 1000000;
    BeamState current_beam[15];
    BeamState next_beam[135];
    BeamState temp;
    current_beam[0].x = pilot->x;
    current_beam[0].y = pilot->y;
    current_beam[0].vx = pilot->vx;
    current_beam[0].vy = pilot->vy;
    current_beam[0].gas = pilot->gas;
    current_beam[0].first_ax = 0;
    current_beam[0].first_ay = 0;
    current_beam[0].score = heatmap[pilot->y][pilot->x];
    *best_acc_x = 0;
    *best_acc_y = 0;
    for (depth = 0; depth < 6; depth++) {
        next_count = 0;
        for (i = 0; i < current_count; i++) {
            if (heatmap[current_beam[i].y][current_beam[i].x] == 0) {
                next_beam[next_count] = current_beam[i];
                next_count++;
                continue;
            }
            for (ax = -1; ax <= 1; ax++) {
                for (ay = -1; ay <= 1; ay++) {
                    if (is_move_valid(current_beam[i].x, current_beam[i].y, current_beam[i].vx, current_beam[i].vy, ax, ay, map)) {
                        next_beam[next_count].vx = current_beam[i].vx + ax;
                        next_beam[next_count].vy = current_beam[i].vy + ay;
                        next_beam[next_count].x = current_beam[i].x + next_beam[next_count].vx;
                        next_beam[next_count].y = current_beam[i].y + next_beam[next_count].vy;
                        in_sand = (map->grid[next_beam[next_count].y][next_beam[next_count].x] == '~') ? 1 : 0;
                        next_beam[next_count].gas = current_beam[i].gas - ((ax * ax + ay * ay) + (int)(sqrt(1.5 * (current_beam[i].vx * current_beam[i].vx + current_beam[i].vy * current_beam[i].vy))) + in_sand);
                        if (next_beam[next_count].gas < 0) {
                            continue;
                        }
                        next_beam[next_count].first_ax = (depth == 0) ? ax : current_beam[i].first_ax;
                        next_beam[next_count].first_ay = (depth == 0) ? ay : current_beam[i].first_ay;
                        next_beam[next_count].score = heatmap[next_beam[next_count].y][next_beam[next_count].x];
                        if (next_beam[next_count].score < min_score) {
                            min_score = next_beam[next_count].score;
                            *best_acc_x = next_beam[next_count].first_ax;
                            *best_acc_y = next_beam[next_count].first_ay;
                        }
                        next_count++;
                    }
                }
            }
        }
        if (next_count == 0) break;
        for (i = 0; i < next_count - 1; i++) {
            for (j = i + 1; j < next_count; j++) {
                score_i = next_beam[i].score - (next_beam[i].gas * 3);
                score_j = next_beam[j].score - (next_beam[j].gas * 3);
                if (score_j < score_i) {
                    temp = next_beam[i];
                    next_beam[i] = next_beam[j];
                    next_beam[j] = temp;
                }
            }
        }
        current_count = (next_count < 15) ? next_count : 15;
        for (i = 0; i < current_count; i++) {
            current_beam[i] = next_beam[i];
        }
    }
}

int main() {
    int initial_gas = 0;
    Map *map;
    int **heatmap;
    PilotState my_pilot;
    int round = 0;
    char line_buffer[MAX_LINE_LENGTH];
    map = read_map(&initial_gas);
    heatmap = generate_heatmap(map);
    my_pilot.gas = initial_gas;
    my_pilot.boosts = BOOSTS_AT_START;
    my_pilot.vx = 0;
    my_pilot.vy = 0;
    fprintf(stderr, "=== >Map Loaded< ===\n");
    fprintf(stderr, "Size %d x %d\n", map->width, map->height);
    fprintf(stderr, "Gas at start %d \n", my_pilot.gas);
    fflush(stderr);
    while (!feof(stdin)) {
        int myX, myY, adv1X, adv1Y, adv2X, adv2Y;
        int acc_x = 0;
        int acc_y = 0;
        if (fgets(line_buffer, MAX_LINE_LENGTH, stdin) == NULL) {
             break;
        }
        sscanf(line_buffer, "%d %d %d %d %d %d", &myX, &myY, &adv1X, &adv1Y, &adv2X, &adv2Y);
        if (round > 0) {
            my_pilot.vx = myX - my_pilot.x;
            my_pilot.vy = myY - my_pilot.y;
        }
        my_pilot.x = myX;
        my_pilot.y = myY;
        round++;
        fprintf(stderr, "=== ROUND %d ===\n", round);
        fprintf(stderr, "Ma Position: (%d, %d) | Vitesse: (%d, %d) | Gas: %d\n", my_pilot.x, my_pilot.y, my_pilot.vx, my_pilot.vy, my_pilot.gas);
        get_best_action(&my_pilot, map, heatmap, &acc_x, &acc_y);
        fprintf(stdout, "%d %d\n", acc_x, acc_y);
        fflush(stdout);
    }
    free_heatmap(heatmap, map->height);
    free_map(map);
    return EXIT_SUCCESS;
}
