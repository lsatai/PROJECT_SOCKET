#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <filesystem>
#include <fstream>
#include <regex>

#pragma comment(lib, "Ws2_32.lib")
#define DEFAULT_BUFLEN 4096
using namespace std;

SOCKET ftpSocket;
SOCKET clamavSocket;
bool passiveMode = true;
bool promptMode = true;
string currentTransferMode = "binary";

bool InitSocket(SOCKET& sock, const string& ip, int port) {
    sockaddr_in addr;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    return connect(sock, (SOCKADDR*)&addr, sizeof(addr)) != SOCKET_ERROR;
}

void SendCommand(SOCKET sock, const string& cmd) {
    send(sock, cmd.c_str(), cmd.length(), 0);
}

string ReceiveResponse(SOCKET sock) {
    char buffer[DEFAULT_BUFLEN];
    int bytes = recv(sock, buffer, DEFAULT_BUFLEN - 1, 0);
    if (bytes <= 0) return "";
    buffer[bytes] = '\0';
    return string(buffer);
}

bool ScanWithClamAV(const string& filepath) {
    if (!filesystem::exists(filepath)) {
        cout << "[ERROR] File not found.\n";
        return false;
    }

    ifstream file(filepath, ios::binary);
    file.seekg(0, ios::end);
    int filesize = file.tellg();
    file.seekg(0, ios::beg);
    vector<char> buffer(filesize);
    file.read(buffer.data(), filesize);

    int sizeNet = htonl(filesize);
    send(clamavSocket, (char*)&sizeNet, sizeof(sizeNet), 0);
    send(clamavSocket, filepath.c_str(), filepath.size(), 0);
    send(clamavSocket, buffer.data(), filesize, 0);

    string result = ReceiveResponse(clamavSocket);
    return result.find("OK") != string::npos;
}

void PrintHelp() {
    cout << R"(Available commands:
ls, cd <dir>, pwd, mkdir <dir>, rmdir <dir>, delete <file>, rename <old> <new>
get <file>, put <file>, mget <wildcard>, mput <wildcard>
ascii, binary, passive, prompt, status
open <ip> <port>, close, quit/bye
help/?
)";
}

void ExecuteFTPCommand(const string& cmd) {
    SendCommand(ftpSocket, cmd + "\r\n");
    string response = ReceiveResponse(ftpSocket);
    cout << response;

    if (cmd.substr(0, 4) == "get ") {
        string filename = cmd.substr(4);
        ofstream out(filename, ios::binary);
        char buf[DEFAULT_BUFLEN];
        int received = recv(ftpSocket, buf, DEFAULT_BUFLEN, 0);
        if (received > 0) out.write(buf, received);
        out.close();
        cout << "[File downloaded]\n";
    }
    else if (cmd.substr(0, 4) == "put ") {
        string filename = cmd.substr(4);
        ifstream file(filename, ios::binary);
        if (file) {
            vector<char> buffer((istreambuf_iterator<char>(file)), {});
            send(ftpSocket, buffer.data(), buffer.size(), 0);
        }
    }
}

void StartClient() {
    string input;
    while (true) {
        cout << "ftp> ";
        getline(cin, input);

        if (input.empty()) continue;
        string cmd = input.substr(0, input.find(' '));
        string args = input.length() > cmd.length() ? input.substr(cmd.length() + 1) : "";

        if (cmd == "put") {
            if (ScanWithClamAV(args)) {
                cout << "[OK] File clean. Uploading...\n";
                ExecuteFTPCommand(input);
            }
            else {
                cout << "[WARNING] File INFECTED. Upload aborted.\n";
            }
        }
        else if (cmd == "mput") {
            for (const auto& entry : filesystem::directory_iterator(".")) {
                if (entry.is_regular_file()) {
                    string filename = entry.path().filename().string();
                    if (!ScanWithClamAV(filename)) {
                        cout << "[SKIPPED] " << filename << " is INFECTED.\n";
                        continue;
                    }
                    if (promptMode) {
                        cout << "Send " << filename << "? (y/n): ";
                        string res; getline(cin, res);
                        if (res != "y") continue;
                    }
                    ExecuteFTPCommand("put " + filename);
                }
            }
        }
        else if (cmd == "get" || cmd == "recv") {
            ExecuteFTPCommand("get " + args);
        }
        else if (cmd == "mget") {
            cout << "[INFO] mget wildcard matching not implemented in client]\n";
        }
        else if (cmd == "open") {
            string ip = args.substr(0, args.find(' '));
            int port = stoi(args.substr(args.find(' ') + 1));
            if (InitSocket(ftpSocket, ip, port)) cout << "[Connected to FTP server]\n";
            else cout << "[ERROR] Could not connect.\n";
        }
        else if (cmd == "close") {
            closesocket(ftpSocket);
            cout << "[Disconnected from FTP server]\n";
        }
        else if (cmd == "passive") {
            passiveMode = !passiveMode;
            cout << "[Passive mode: " << (passiveMode ? "ON" : "OFF") << "]\n";
        }
        else if (cmd == "prompt") {
            promptMode = !promptMode;
            cout << "[Prompt mode: " << (promptMode ? "ON" : "OFF") << "]\n";
        }
        else if (cmd == "ascii" || cmd == "binary") {
            currentTransferMode = cmd;
            ExecuteFTPCommand(cmd);
        }
        else if (cmd == "status") {
            cout << "Transfer mode: " << currentTransferMode << "\n";
            cout << "Passive: " << (passiveMode ? "ON" : "OFF") << "\n";
            cout << "Prompt: " << (promptMode ? "ON" : "OFF") << "\n";
        }
        else if (cmd == "quit" || cmd == "bye") {
            break;
        }
        else if (cmd == "help" || cmd == "?") {
            PrintHelp();
        }
        else {
            ExecuteFTPCommand(input);
        }
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    string clamIP = "127.0.0.1";
    int clamPort = 8888;

    if (!InitSocket(clamavSocket, clamIP, clamPort)) {
        cerr << "Failed to connect to ClamAV agent.\n";
        return 1;
    }
    cout << "[Connected to ClamAV Agent]\n";

    StartClient();

    closesocket(clamavSocket);
    WSACleanup();
    return 0;
}
