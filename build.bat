@echo off
setlocal
set "PATH=C:\msys64\mingw64\bin;%PATH%"
g++ -std=c++17 -Wall -Wextra -O2 -pthread client.c++ -o client.exe || goto :err
g++ -std=c++17 -Wall -Wextra -O2 -pthread server.c++ -o server.exe || goto :err
echo.
echo Compilacao concluida: server.exe e client.exe
goto :eof
:err
echo.
echo ERRO na compilacao.
endlocal