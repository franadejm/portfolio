#include <stdio.h>
#include <stdlib.h>


int main() {
    
    char buffer[60];
	printf ("What's your name? ");
    gets(buffer);
	printf("Your name is %s\n", buffer);
    return EXIT_SUCCESS;
}