#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>


void test(const char* sensor_type, char* filepath);


/* demo.c:  My first C program on a Linux */
int main(void)
{
    unsigned long* foo = malloc(5 * sizeof(unsigned long));
    free(foo);


    char* sensor_type = "TMP";
    char filepath[64];

    test(sensor_type, filepath);


    struct stat st;
    if (stat("s1.c", &st) == 0) {
        printf("File s1.c size is %li bytes\n", st.st_size);
    }

    static struct timeval now;
    now.tv_sec = 5559483 / 1000;

    time_t t1 = 1560 + 2903;
    printf("Default time is %f\n", (double)now.tv_sec + (double)now.tv_usec / 1000000);

    gettimeofday(&now, NULL);
    printf("Default time is %f\n", (double)now.tv_sec + (double)now.tv_usec / 1000000);

    return 0;
}

void test(const char* sensor_type, char* filepath)
{
    sprintf(filepath, "/spiffs/%s.txt", sensor_type);
    printf("FILEPATH: %s\n", filepath);

}