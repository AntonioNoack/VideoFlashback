#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

int main() {
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/videoflashback.sock", sizeof(addr.sun_path) - 1);

    if (connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to VideoFlashback service. Is it running?\n";
        close(client_fd);
        return 1;
    }

    const char* cmd = "S";
    if (write(client_fd, cmd, 1) < 0) {
        std::cerr << "Failed to send trigger command\n";
        close(client_fd);
        return 1;
    }

    std::cout << "Replay save triggered successfully.\n";
    close(client_fd);
    return 0;
}
