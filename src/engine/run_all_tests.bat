@echo off
setlocal
echo ========================================
echo   LoMo Engine - Full Functional Test
echo ========================================
echo.

echo [1/3] Building Test Components...
call build_engine.bat >nul 2>&1
cl /nologo /utf-8 /O2 benchmark_ingest.c ingestor.c storage_engine.c Cabinet.lib /Fe:benchmark_ingest.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    exit /b %errorlevel%
)
echo [OK] Build Successful.

echo.
echo [2/3] Running Core Engine Tests...
echo --- 1. Ingestion Test ---
test_ingest.exe
echo.
echo --- 2. Partition Merge Test ---
test_merge.exe
echo.
echo --- 3. SQL Parser Test ---
lomo_sql.exe "SELECT symbol, bid FROM v1 WHERE symbol='BTCUSDT' LIMIT 10"

echo.
echo [3/3] Running Performance Benchmark...
echo Running ingestion benchmark (100,000 rows)...
benchmark_ingest.exe > benchmark_results.txt

echo.
echo ========================================
echo  All Tests Completed Successfully!
echo  Benchmark saved to: src\engine\benchmark_results.txt
echo ========================================
echo.
type benchmark_results.txt
endlocal
