#include <windows.h>
#include <stdio.h>

int main() 
{
    HMODULE hKernel32 = LoadLibrary("kernel32.dll");
    FARPROC winExecAddr = GetProcAddress(hKernel32, "WinExec");

    printf("WinExec Address: 0x%p\n", winExecAddr);
    return 0;
}