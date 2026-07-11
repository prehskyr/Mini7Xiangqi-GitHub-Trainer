@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not exist ".git" (
  echo ERROR: Extract this package into the root of Mini7Xiangqi-GitHub-Trainer.
  echo The current directory does not contain .git
  pause
  exit /b 1
)

if not exist "native\engine\src\Makefile" (
  echo ERROR: native engine files are incomplete.
  pause
  exit /b 1
)

where git >nul 2>nul
if errorlevel 1 (
  echo ERROR: Git is not available in PATH.
  pause
  exit /b 1
)

echo Pulling the latest main branch...
git pull --rebase origin main
if errorlevel 1 goto :failed

echo Staging native engine and GitHub workflows...
git add native .github/workflows/native-engine-ci.yml .github/workflows/generate-native-data.yml .github/workflows/train-native-nnue-cpu.yml .github/workflows/release-native-engine.yml INSTALL_NATIVE_NNUE.txt commit_native_pipeline.cmd
if errorlevel 1 goto :failed

git diff --cached --quiet
if not errorlevel 1 (
  echo No new changes need to be committed.
) else (
  git commit -m "feat: add native Mini7 NNUE GitHub pipeline"
  if errorlevel 1 goto :failed
)

git push origin main
if errorlevel 1 goto :failed

echo.
echo Native NNUE pipeline pushed successfully.
echo Next: open GitHub Actions and inspect "Native Mini7 engine CI".
pause
exit /b 0

:failed
echo.
echo Operation failed. Read the Git error above; no local training was started.
pause
exit /b 1

git add -f native/engine/src/Makefile native/engine/src/Makefile_js

