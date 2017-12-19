#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


void test(const char* sensor_type, char* filepath);


/* demo.c:  My first C program on a Linux */
int main(void)
{
    // array assignment test
    typedef struct {
        uint8_t ssid[32];
        uint8_t passwd[64];
    } foo_struct;

    const char *ssid = "something";
    const char *passwd = "password";

    foo_struct bar = {
        .ssid = {0},
        .passwd = {0},
    };
    strcpy(&bar.ssid, ssid);
    strcpy(&bar.passwd, passwd);

    foo_struct b1 = {
        .ssid = "something",
        .passwd = "password",
    };

    for (int i=0; i < 20; i++) {
        printf("%d-th elements are %hhu and %hhu\n", i, bar.ssid[i], b1.ssid[i]);
    }

    if (bar.ssid[9] == b1.ssid[9]) {
        printf("BAR SSID length is %d\n", strlen(bar.ssid));
        printf("B1 SSID length is %d\n", strlen(b1.ssid));
        
        printf("BAR SSID is %s\n", bar.ssid);
        printf("B1 SSID is %s\n", b1.ssid);    
    }


    // memory free test
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