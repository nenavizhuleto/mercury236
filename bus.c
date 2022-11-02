#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define byte unsigned char
#define UInt16 uint16_t

typedef struct
{
    byte address;
    byte command;
    UInt16 CRC;
} TestCmd;

int main()
{

    int network_socket;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9002);
    addr.sin_addr.s_addr = INADDR_ANY;

    network_socket = socket(AF_INET, SOCK_STREAM, 0);

    bind(network_socket, (struct sockaddr *)&addr, sizeof(addr));
    listen(network_socket, 5);

    {
        char addr_r[255];
        inet_ntop(AF_INET, &addr.sin_addr.s_addr, addr_r, sizeof(addr_r));
        printf("Bus Address: %s\n\r", addr_r);
    }

    int m;

    struct sockaddr_in m_addr;
    socklen_t addr_len;

    m = accept(network_socket, (struct sockaddr *)&m_addr, &addr_len);

    {
        char addr_r[255];

        inet_ntop(AF_INET, &m_addr.sin_addr.s_addr, addr_r, sizeof(addr_r));

        printf("Master Address: %s\n\r", addr_r);
    }

    while (1)
    {
        char buffer[256];
        int len = recv(m, &buffer, sizeof(buffer), 0);
        // printf("len: %d\n\r", len);
        if (len > 0)
        {
            printf("Recieved: %lX\n\r", buffer);
        }
        send(m, "321", sizeof(TestCmd), 0);
        // usleep(1000000);
    }

    close(m);
    close(network_socket);

    return 0;
}