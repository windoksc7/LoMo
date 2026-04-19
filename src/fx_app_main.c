#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fx_ingestor.h"
#include "fx_analytics.h"
#include "../engine/storage_engine.h"
#include "fx_api.h"
#include "license.h"
#include <malloc.h>
#include <windows.h>
#include <math.h>

void cleanup_partitions() {
    printf("\n[System] Cleaning up temporary partitions... Done.\n");
    system("powershell.exe -NoProfile -Command \"Remove-Item -Recurse -Force fx_part_* -ErrorAction SilentlyContinue\"");
}

void print_fx_help() {
    printf("\nLoMo Ace-Concentration CLI\n");
    printf("  ingest <log_file> <part_dir> - Ingest raw ticks into LoMo\n");
    printf("  hunt <part_dir_base>        - [REAL-TIME] Start Top-20 Strategic Hunt\n");
    printf("  exit                        - Exit\n");
}

void run_multi_hunt(const char* part_base) {
    const char* symbols[] = {
        "BTCUSDT", "ETHUSDT", "SOLUSDT", "BNBUSDT", "XRPUSDT", 
        "ADAUSDT", "AVAXUSDT", "DOTUSDT", "LINKUSDT", "DOGEUSDT",
        "MATICUSDT", "LTCUSDT", "BCHUSDT", "TRXUSDT", "NEARUSDT",
        "ATOMUSDT", "UNIUSDT", "ICPUSDT", "APTUSDT", "OPUSDT"
    };
    int sym_count = 20;
    double initial_seed = 8000.0;
    double balance = initial_seed;
    double leverage = 1.2;
    double slip = 0.0001; 
    int total_trades = 0;
    int total_wins = 0;
    int kill_switch = 0;

    printf("\n======================================================================\n");
    printf("   LoMo TOP-20 STRATEGIC HUNTING DASHBOARD (Slippage: 0.01%%)\n");
    printf("======================================================================\n");
    printf("%-12s | %-12s | %-12s | %-10s | %-10s\n", "Symbol", "PnL (JPY)", "Win Rate", "Trades", "Status");
    printf("----------------------------------------------------------------------\n");

    for (int s = 0; s < sym_count; s++) {
        if (kill_switch) break;
        
        const char* symbol = symbols[s];
        
        // --- RE-CALIBRATED ASSET STRATEGY ---
        size_t window = 15000; double conf = 0.0020; double t_start = 0.0200; // Default Sentinel
        
        if (strcmp(symbol, "BTCUSDT") == 0 || strcmp(symbol, "DOTUSDT") == 0 || strcmp(symbol, "LINKUSDT") == 0) {
            window = 10000; conf = 0.0015; t_start = 0.0162; // Elite Group A
        } else if (strcmp(symbol, "SOLUSDT") == 0) {
            window = 12000; conf = 0.0018; t_start = 0.0198; // Elite: SOL
        } else if (strcmp(symbol, "NEARUSDT") == 0) {
            window = 15000; conf = 0.0020; t_start = 0.0200; // THE GOLDEN NEAR (Restored)
        } else {
            window = 30000; conf = 0.0050; t_start = 0.0400; // Silent Sentinels
        }

        double sym_start_bal = balance;
        int sym_trades = 0; int sym_wins = 0;

        for (int c = 0; c < 100; c++) {
            if (balance < initial_seed * 0.95) { kill_switch = 1; break; }
            char chunk_dir[256]; sprintf(chunk_dir, "%s_%d", part_base, c);
            LomoPartHeader* part = lomo_load_part(chunk_dir);
            if (!part) break;
            size_t matched_count = 0, start_idx = 0;
            size_t sym_sz = (size_t)part->columns[1].uncompressed_size;
            uint8_t* sym_all = (uint8_t*)_aligned_malloc(sym_sz, 32);
            lomo_read_column_simd(part, 1, sym_all, sym_sz);
            uint8_t* ptr = sym_all;
            for (size_t i = 0; i < (size_t)part->total_rows; i++) {
                uint64_t len = *(uint64_t*)ptr;
                if (len == strlen(symbol) && memcmp(ptr + 8, symbol, len) == 0) { if (matched_count == 0) start_idx = i; matched_count++; }
                ptr += (8 + len);
            }
            if (matched_count > 0) {
                int64_t* bid_all = (int64_t*)_aligned_malloc(part->total_rows * 8, 32);
                lomo_read_column_simd(part, 2, bid_all, part->total_rows * 8);
                FX_BacktestResult r = fx_run_ace_simulation(bid_all + start_idx, matched_count, balance, window, leverage, conf, t_start, 0.0060, slip);
                balance = r.final_balance; sym_trades += r.trades; total_trades += r.trades;
                sym_wins += (int)(r.win_rate * r.trades); total_wins += (int)(r.win_rate * r.trades);
                _aligned_free(bid_all);
            }
            _aligned_free(sym_all); lomo_free_part(part);
        }
        double sym_pnl = balance - sym_start_bal;
        double sym_wr = (sym_trades > 0) ? ((double)sym_wins / sym_trades) * 100.0 : 0.0;
        const char* status = (sym_pnl > 0) ? "PROFIT" : (sym_pnl < 0) ? "LOSS" : "IDLE";
        if (kill_switch) status = "KILLED";
        printf("%-12s | %-+12.2f | %-11.2f%% | %-10d | [%s]\n", symbol, sym_pnl, sym_wr, sym_trades, status);
    }

    double total_growth = ((balance - initial_seed) / initial_seed) * 100.0;
    double total_wr = (total_trades > 0) ? ((double)total_wins / total_trades) * 100.0 : 0.0;

    printf("----------------------------------------------------------------------\n");
    printf(">>> FINAL ACCOUNT SUMMARY <<<\n");
    printf("Current Balance : %.2f JPY\n", balance);
    printf("Total Growth    : %+.2f%%\n", total_growth);
    printf("Overall Win Rate: %.2f%%\n", total_wr);
    printf("Total Trades    : %d\n", total_trades);
    if (kill_switch) printf("!!! SEED PROTECTION ACTIVATED (-5%% KILL-SWITCH HIT) !!!\n");
    printf("======================================================================\n");
}

int main(int argc, char** argv) {
    atexit(cleanup_partitions);
    printf("========================================\n   LoMo TOP-20 Strategic Arena v1.0\n========================================\n");
    printf("[Network] Checking Binance API Latency... ");
    FX_NetworkStatus net = fx_check_binance_latency();
    if (net.status_code == 0) printf("GREEN (%.2f ms)\n", net.latency_ms);
    else if (net.status_code == 1) printf("YELLOW (%.2f ms)\n", net.latency_ms);
    else { printf("RED (%.2f ms) - High Latency!\n", net.latency_ms); return 1; }

    if (argc > 1) { if (strcmp(argv[1], "ingest") == 0 && argc >= 4) { fx_ingest_log(argv[2], argv[3]); return 0; } }
    char cmd[256]; print_fx_help();
    while (1) {
        printf("fx> "); if (!fgets(cmd, sizeof(cmd), stdin)) break;
        cmd[strcspn(cmd, "\n")] = 0; if (strlen(cmd) == 0) continue; if (strcmp(cmd, "exit") == 0) break;
        char temp[256]; strcpy(temp, cmd); char* token = strtok(temp, " "); if (!token) continue;
        char* arg1 = strtok(NULL, " ");
        if (strcmp(token, "ingest") == 0) { char* arg2 = strtok(NULL, " "); if (arg1 && arg2) fx_ingest_log(arg1, arg2); }
        else if (strcmp(token, "hunt") == 0) { if (arg1) run_multi_hunt(arg1); }
        else { print_fx_help(); }
    }
    return 0;
}
