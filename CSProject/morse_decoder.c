#include <stdio.h>
#include <string.h>

// Define Morse code mappings
typedef struct {
    char character;
    char *morse;
} MorseCode;

MorseCode morseTable[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
    {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
    {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'0', "-----"}
};

// Function to decode a single Morse code symbol
char morseToLetter(char *morse) {
    int comparison;
    for (int i = 0; i < sizeof(morseTable) / sizeof(MorseCode); i++) {
        comparison = strcmp(morse, morseTable[i].morse);
        if(comparison == 0) return morseTable[i].character;
    }
    return '?';
}

int main() {
    char input[1000];
    printf("Enter Morse code (use spaces to separate symbols): ");
    fgets(input, sizeof(input), stdin);
    
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }

    char *token = strtok(input, " ");
    while (token != NULL) {
        printf("%c", morseToLetter(token));
        token = strtok(NULL, " ");
        if (token != NULL && *token == '\0') {
            printf(" ");
            token = strtok(NULL, " ");
        }
    }

    return 0;
}
