@echo off
echo Building LoMo Engine Components (HAL-Aware)...
cl /utf-8 /O2 scanner_core.c os_win.c ingestor.c storage_engine.c simd_filter.c Cabinet.lib /Fe:scanner.exe
cl /utf-8 /O2 test_ingestion.c os_win.c ingestor.c storage_engine.c Cabinet.lib /Fe:test_ingest.exe
cl /utf-8 /O2 query_engine.c os_win.c storage_engine.c simd_filter.c Cabinet.lib /Fe:query_lomo.exe
cl /utf-8 /O2 test_merge.c os_win.c ingestor.c storage_engine.c merger.c Cabinet.lib /Fe:test_merge.exe
cl /utf-8 /O2 test_sql.c os_win.c sql_parser.c storage_engine.c simd_filter.c Cabinet.lib /Fe:lomo_sql.exe
cl /utf-8 /O2 engine_main.c os_win.c ingestor.c storage_engine.c sql_parser.c simd_filter.c Cabinet.lib /Fe:lomo_cli.exe
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b %errorlevel%
)
echo Build successful: lomo_cli.exe, scanner.exe, test_ingest.exe, query_lomo.exe, test_merge.exe, lomo_sql.exe
