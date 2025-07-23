#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 4096
#define TEMP_FILENAME "received_file.tmp"

#include <windows.h>
#include <string>
#include <iostream>

std::string scanFileWithTimeout(const std::string& filepath, int timeoutMillis) {
    std::string command = "\"D:\\HCMUS\\Internet\\VirusScanningUltils\\clamav-1.4.3.win.x64\\clamav-1.4.3.win.x64\\clamscan.exe\" " + filepath;


    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    // Create the process
    if (!CreateProcessA(
            NULL,
            (LPSTR)command.c_str(),  // Command line
            NULL, NULL, FALSE,
            CREATE_NO_WINDOW,
            NULL, NULL,
            &si, &pi)) {
        std::cerr << "Failed to launch clamscan.\n";
        return "ERROR";
    }

    // Wait for the process with timeout
    DWORD result = WaitForSingleObject(pi.hProcess, timeoutMillis);

    if (result == WAIT_TIMEOUT) {
        std::cerr << "Scan timed out!\n";
        TerminateProcess(pi.hProcess, 1); // kill process
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return "TIMEOUT";
    }

    // Check exit code
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    // Clean up
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Exit code 0 means clean
    if (exitCode == 0)
        return "OK";
    else if (exitCode == 1)
        return "INFECTED";
    else
        return "ERROR";
}


int main() {
    WSADATA wsaData;
    SOCKET serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int addrLen = sizeof(clientAddr);
    char buffer[BUFFER_SIZE];

    // 1. Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    // 2. Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        WSACleanup();
        return 1;
    }

    // 3. Bind
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 4. Listen
    if (listen(serverSocket, 1) == SOCKET_ERROR) {
        std::cerr << "Listen failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "ClamAVAgent is listening on port " << PORT << "..." << std::endl;

    // 5. Accept client
    clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Accept failed." << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 6. Receive file
    std::ofstream outFile(TEMP_FILENAME, std::ios::binary);
    int bytesRead;
    while ((bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        outFile.write(buffer, bytesRead);
        if (bytesRead < BUFFER_SIZE) break; // End of transmission
    }
    outFile.close();

    // 7. Run ClamAV scan
    std::string result = scanFileWithTimeout(TEMP_FILENAME, 30000); // 30 sec timeout (in miliseconds)
    std::cout << "Scan result: " << result << std::endl;

    // 8. Send result back to client
    send(clientSocket, result.c_str(), result.length(), 0);

    // 9. Cleanup
    closesocket(clientSocket);
    closesocket(serverSocket);
    remove(TEMP_FILENAME);
    remove("scan_result.txt");
    WSACleanup();

    return 0;
}
