#include "esp_spiffs.h"


int count_line_number(char* filepath) {
    int lines = 0;
    int ch;

    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        return 0;
    }

    while( !feof(f) ) {
        ch = fgetc(f);

        if(ch == '\n') {
            lines++;
        }
    }
    fclose(f);

    return lines;
}
