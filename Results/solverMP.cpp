#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <omp.h>
#include <chrono>

using namespace std;

// Tvar quatromina reprezentovaný pomocí relativních offsetů vůči [0,0]
struct Shape {
    char type;
    vector<pair<int, int>> offsets;
};

// Všech 8 unikátních rotací/zrcadlení pro T a Z.
// Definováno v reading order (shora dolů, zleva doprava)
const vector<Shape> SHAPES = {
    {'T', {{0,0}, {0,1}, {0,2}, {1,1}}},      
    {'T', {{0,0}, {1,-1}, {1,0}, {1,1}}},     
    {'T', {{0,0}, {1,0}, {1,1}, {2,0}}},      
    {'T', {{0,0}, {1,-1}, {1,0}, {2,0}}},     
    {'Z', {{0,0}, {0,1}, {1,1}, {1,2}}},      
    {'Z', {{0,0}, {1,-1}, {1,0}, {2,-1}}},    
    {'Z', {{0,0}, {0,1}, {1,-1}, {1,0}}},     
    {'Z', {{0,0}, {1,0}, {1,1}, {2,1}}}       
};

class QuatrominoSolver {
private:
    int R, C;
    vector<vector<int>> values;
    
    vector<vector<int>> best_board;
    vector<char> best_piece_types;

    int best_cost;
    int trivial_lower_bound;
    bool optimal_found;

    // Spočítá triviální dolní mez jako součet k nejmenších hodnot na desce,
    // kde k = (R * C) mod 4.
    void calculate_trivial_lower_bound() {
        vector<int> all_values;
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) {
                all_values.push_back(values[r][c]);
            }
        }
        sort(all_values.begin(), all_values.end());
        
        int k = (R * C) % 4;
        trivial_lower_bound = 0;
        for (int i = 0; i < k; ++i) {
            trivial_lower_bound += all_values[i];
        }
    }

    // Kontrola kolizí s okraji desky a jinými dílky
    bool can_place(const vector<vector<int>>& board, int r, int c, const Shape& shape) {
        for (const auto& offset : shape.offsets) {
            int nr = r + offset.first;
            int nc = c + offset.second;
            if (nr < 0 || nr >= R || nc < 0 || nc >= C) return false;
            if (board[nr][nc] != 0) return false;
        }
        return true;
    }

    // Zápis dílku na desku
    void place(vector<vector<int>>& board, vector<char>& p_types, int r, int c, const Shape& shape, int id, char type) {
        for (const auto& offset : shape.offsets) {
            board[r + offset.first][c + offset.second] = id;
        }
        if (id >= p_types.size()) p_types.resize(id + 1);
        p_types[id] = type;
    }

    // Smazání dílku (používáno pro backtracking)
    void remove(vector<vector<int>>& board, int r, int c, const Shape& shape) {
        for (const auto& offset : shape.offsets) {
            board[r + offset.first][c + offset.second] = 0;
        }
    }

    // Bezpečně přepíše nejlepší řešení
    void update_best(int cost, const vector<vector<int>>& board, const vector<char>& p_types) {
        #pragma omp critical
        {
            if (cost < best_cost) {
                best_cost = cost;
                best_board = board;
                best_piece_types = p_types;
                if (best_cost == trivial_lower_bound) {
                    #pragma omp atomic write
                    optimal_found = true;
                }
            }
        }
    }

    // Hlavní sekvenční BB-DFS rekurze
    void solve_dfs_seq(int cell_idx, int current_cost, int t_count, int z_count, 
                       vector<vector<int>>& board, vector<char>& p_types, int& p_id) {
        
        bool opt;
        #pragma omp atomic read
        opt = optimal_found;
        if (opt) return;

        int b_cost;
        #pragma omp atomic read
        b_cost = best_cost;
        if (current_cost >= b_cost) return;

        int remaining_cells = R * C - cell_idx;
        int max_possible_pieces = remaining_cells / 4;
        if (abs(t_count - z_count) > max_possible_pieces + 1) return;

        if (cell_idx == R * C) {
            if (current_cost < best_cost) {
                if (abs(t_count - z_count) <= 1) { 
                    update_best(current_cost, board, p_types);
                }
            }
            return;
        }

        int r = cell_idx / C;
        int c = cell_idx % C;

        if (board[r][c] != 0) {
            solve_dfs_seq(cell_idx + 1, current_cost, t_count, z_count, board, p_types, p_id);
            return;
        }

        for (const auto& shape : SHAPES) {
            if (can_place(board, r, c, shape)) {
                place(board, p_types, r, c, shape, p_id, shape.type);
                
                int next_t = t_count + (shape.type == 'T' ? 1 : 0);
                int next_z = z_count + (shape.type == 'Z' ? 1 : 0);
                
                p_id++;
                solve_dfs_seq(cell_idx + 1, current_cost, next_t, next_z, board, p_types, p_id);
                p_id--;
                
                remove(board, r, c, shape);
            }
        }

        solve_dfs_seq(cell_idx + 1, current_cost + values[r][c], t_count, z_count, board, p_types, p_id);
    }

    // Paralelní task generátor
    void solve_dfs_task(int cell_idx, int current_cost, int t_count, int z_count, 
                        vector<vector<int>> board, vector<char> p_types, int p_id) {
        
        bool opt;
        #pragma omp atomic read
        opt = optimal_found;
        if (opt) return;

        int b_cost;
        #pragma omp atomic read
        b_cost = best_cost;
        if (current_cost >= b_cost) return;

        int remaining_cells = R * C - cell_idx;
        int max_possible_pieces = remaining_cells / 4;
        if (abs(t_count - z_count) > max_possible_pieces + 1) return;

        if (cell_idx == R * C) {
            if (current_cost < best_cost) {
                if (abs(t_count - z_count) <= 1) { 
                    update_best(current_cost, board, p_types);
                }
            }
            return;
        }

        int r = cell_idx / C;
        int c = cell_idx % C;

        if (board[r][c] != 0) {
            solve_dfs_task(cell_idx + 1, current_cost, t_count, z_count, board, p_types, p_id);
            return;
        }

        if (cell_idx < TASK_CUTOFF) {
            for (const auto& shape : SHAPES) {
                if (can_place(board, r, c, shape)) {
                    vector<vector<int>> next_board = board;
                    vector<char> next_p_types = p_types;
                    place(next_board, next_p_types, r, c, shape, p_id, shape.type);
                    
                    int next_t = t_count + (shape.type == 'T' ? 1 : 0);
                    int next_z = z_count + (shape.type == 'Z' ? 1 : 0);
                    
                    #pragma omp task shared(best_cost, optimal_found)
                    solve_dfs_task(cell_idx + 1, current_cost, next_t, next_z, next_board, next_p_types, p_id + 1);
                }
            }

            #pragma omp task shared(best_cost, optimal_found)
            solve_dfs_task(cell_idx + 1, current_cost + values[r][c], t_count, z_count, board, p_types, p_id);
            
            #pragma omp taskwait
        } else {
            solve_dfs_seq(cell_idx, current_cost, t_count, z_count, board, p_types, p_id);
        }
    }

public:
    int TASK_CUTOFF = 8; 

    // Načte matici ze zadaného txt souboru
    bool load_from_file(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) return false;

        if (!(file >> R >> C)) return false;
        
        values.assign(R, vector<int>(C));
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) {
                file >> values[r][c];
            }
        }
        return true;
    }

    // Inicializuje proměnné a spustí OpenMP paralelní blok
    void solve() {
        vector<vector<int>> initial_board(R, vector<int>(C, 0));
        vector<char> initial_piece_types;
        
        best_board.assign(R, vector<int>(C, 0));
        best_cost = 1000000000; 
        optimal_found = false;

        calculate_trivial_lower_bound();

        #pragma omp parallel
        {
            #pragma omp single
            {
                solve_dfs_task(0, 0, 0, 0, initial_board, initial_piece_types, 1);
            }
        }
    }

    // Vypíše hotovou matici a nejlepší cenu
    void print_solution() {
        if (best_cost == 1000000000) {
            cout << "Zadne platne pokryti nebylo nalezeno." << endl;
            return;
        }

        cout << "-----------------------------------" << endl;
        cout << "Minimalni nalezena cena: " << best_cost << endl;
        cout << "Vysledna matice pokryti:" << endl;
        
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) {
                int id = best_board[r][c];
                if (id == 0) {
                    cout << setw(4) << values[r][c] << " ";
                } else {
                    string piece = string(1, best_piece_types[id]) + to_string(id);
                    cout << setw(4) << piece << " ";
                }
            }
            cout << "\n";
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Pouziti: " << argv[0] << " <cesta_k_souboru_mapy> [cutoff_hloubka]" << endl;
        return 1;
    }

    QuatrominoSolver solver;
    
    if (argc >= 3) {
        solver.TASK_CUTOFF = atoi(argv[2]);
    }

    if (solver.load_from_file(argv[1])) {
        auto start = std::chrono::high_resolution_clock::now();
        solver.solve();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        cout << diff.count() << endl;
        //solver.print_solution();
    }

    return 0;
}