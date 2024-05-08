C_FLAGS=-Wall
C_FLAGS+=-Wextra
C_FLAGS+=-pedantic
C_FLAGS+=-std=c99

editor: main.c
	gcc $(C_FLAGS) main.c -o editor

