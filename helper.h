#define PORT 8080
//Raspunsuri server
#define RESPONSE_SUCCES 0x0
#define RESPONSE_FILE_NOT_FOUND 0x1
#define RESPONSE_PERMISSION_DENIED 0x2
#define RESPONSE_OUT_OF_MEMORY 0x4
#define RESPONSE_SERVER_BUSY 0x8
#define RESPONSE_UNKNOWN_OPERATION 0x10
#define RESPONSE_BAD_ARGUMENTS 0x20
#define RESPONSE_OTHER_ERROR 0x40
//Operatii client
#define LIST 0x0
#define DOWNLOAD 0x1
#define UPLOAD 0x2
#define DELETE 0x4
#define MOVE 0x8
#define UPDATE 0x10
#define SEARCH 0x20
//Extra
#define MAX_THREADS 10
#define MAX_CLIENTS 2
#define ACK 0xFF
#define ACK_OK 0xFE
#define LOG_FILE "operations.txt"