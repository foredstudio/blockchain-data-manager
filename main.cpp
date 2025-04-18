// File: main.cpp

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

// Тип транзакции
enum TransactionType { REGISTER, GRANT, REVOKE, REQUEST };

// Одна транзакция
struct Transaction {
    TransactionType type{};
    string owner;
    string dataHash;
    string metadata;
    string recipient;
    string requester;
    time_t timestamp{};
};

// Один блок
struct Block {
    vector<Transaction> transactions;
    string prevHash;
    string hash;
    time_t timestamp{};
};

// Простейший хеш-функция (DJB2)
string simpleHash(const string &input) {
    unsigned long h = 5381;
    for (unsigned char c : input) {
        h = ((h << 5) + h) + c;
    }
    return to_string(h);
}

// Собираем содержимое блока в строку и хешируем
string calculateBlockHash(const Block &block) {
    ostringstream oss;
    oss << block.prevHash << block.timestamp;
    for (const auto &t : block.transactions) {
        oss << static_cast<int>(t.type)
            << t.owner
            << t.dataHash
            << t.metadata
            << t.recipient
            << t.requester
            << t.timestamp;
    }
    return simpleHash(oss.str());
}

// Декодирование percent-encoding (URL decoding)
string urlDecode(const string &s) {
    string result;
    char buf[3] = {0};
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            buf[0] = s[i+1];
            buf[1] = s[i+2];
            result += static_cast<char>(strtol(buf, nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

// Парсинг body вида key1=val1&key2=val2
map<string, string> parseBodyAll(const string &body) {
    map<string, string> params;
    size_t start = 0;
    while (start < body.size()) {
        size_t eq = body.find('=', start);
        if (eq == string::npos) break;
        string key = urlDecode(body.substr(start, eq - start));
        size_t amp = body.find('&', eq);
        string value = (amp == string::npos)
            ? body.substr(eq + 1)
            : body.substr(eq + 1, amp - eq - 1);
        params[key] = urlDecode(value);
        if (amp == string::npos) break;
        start = amp + 1;
    }
    return params;
}

// Формируем HTTP-ответ
string httpResponse(const string &body) {
    ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Content-Type: text/plain\r\n\r\n"
        << body;
    return oss.str();
}

// Блокчейн
class Blockchain {
public:
    vector<Block> chain;
    mutex mtx;

    Blockchain() {
        Block genesis{};
        genesis.timestamp = time(nullptr);
        genesis.prevHash  = "0";
        genesis.hash      = simpleHash("genesis" + to_string(genesis.timestamp));
        chain.push_back(genesis);
    }

    void addBlock(const Block &block) {
        lock_guard<mutex> lock(mtx);
        chain.push_back(block);
    }
};

// Менеджер данных
class DataManager {
public:
    map<string, vector<string>> ownerData;
    map<string, vector<string>> accessList;
    mutex mtx;

    bool registerData(const string &owner, const string &dataHash, const string &metadata) {
        lock_guard<mutex> lock(mtx);
        ownerData[owner].push_back(dataHash);
        return true;
    }

    bool grantAccess(const string &owner, const string &dataHash, const string &recipient) {
        lock_guard<mutex> lock(mtx);
        auto it = ownerData.find(owner);
        if (it == ownerData.end()) return false;
        if (!count(it->second.begin(), it->second.end(), dataHash)) return false;
        accessList[dataHash].push_back(recipient);
        return true;
    }

    bool revokeAccess(const string &owner, const string &dataHash, const string &recipient) {
        lock_guard<mutex> lock(mtx);
        auto it = ownerData.find(owner);
        if (it == ownerData.end()) return false;
        if (!count(it->second.begin(), it->second.end(), dataHash)) return false;
        auto &vec = accessList[dataHash];
        auto rit = find(vec.begin(), vec.end(), recipient);
        if (rit == vec.end()) return false;
        vec.erase(rit);
        return true;
    }

    bool requestAccess(const string &requester, const string &dataHash) {
        lock_guard<mutex> lock(mtx);
        auto it = accessList.find(dataHash);
        if (it == accessList.end()) return false;
        return count(it->second.begin(), it->second.end(), requester);
    }
};

Blockchain blockchain;
DataManager dataManager;

// Обработка одного подключения
void processRequest(int clientSock) {
    constexpr int BUF_SIZE = 8192;
    char buffer[BUF_SIZE];
    memset(buffer, 0, sizeof(buffer));
    int received = recv(clientSock, buffer, sizeof(buffer)-1, 0);
    if (received <= 0) {
        close(clientSock);
        return;
    }

    istringstream req(buffer);
    string method, path, protocol;
    req >> method >> path >> protocol;

    // Парсим заголовки
    string line;
    int contentLength = 0;
    while (getline(req, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        if (line.rfind("Content-Length:", 0) == 0) {
            contentLength = stoi(line.substr(15));
        }
    }

    // Читаем тело, если есть
    string body;
    if (contentLength > 0) {
        body.resize(contentLength);
        req.read(&body[0], contentLength);
    }

    string responseBody;
    if (method == "POST") {
        auto params = parseBodyAll(body);
        Transaction t{};
        Block block{};
        block.timestamp = time(nullptr);
        block.prevHash  = blockchain.chain.back().hash;

        if (path == "/register") {
            t.type     = REGISTER;
            t.owner    = params["owner"];
            t.dataHash = params["dataHash"];
            t.metadata = params["metadata"];
            t.timestamp = time(nullptr);

            bool ok = dataManager.registerData(t.owner, t.dataHash, t.metadata);
            block.transactions.push_back(t);
            block.hash = calculateBlockHash(block);
            blockchain.addBlock(block);
            responseBody = ok ? "Registration successful" : "Registration failed";

        } else if (path == "/grant") {
            t.type      = GRANT;
            t.owner     = params["owner"];
            t.dataHash  = params["dataHash"];
            t.recipient = params["recipient"];
            t.timestamp = time(nullptr);

            bool ok = dataManager.grantAccess(t.owner, t.dataHash, t.recipient);
            block.transactions.push_back(t);
            block.hash = calculateBlockHash(block);
            blockchain.addBlock(block);
            responseBody = ok ? "Access granted" : "Grant failed";

        } else if (path == "/revoke") {
            t.type      = REVOKE;
            t.owner     = params["owner"];
            t.dataHash  = params["dataHash"];
            t.recipient = params["recipient"];
            t.timestamp = time(nullptr);

            bool ok = dataManager.revokeAccess(t.owner, t.dataHash, t.recipient);
            block.transactions.push_back(t);
            block.hash = calculateBlockHash(block);
            blockchain.addBlock(block);
            responseBody = ok ? "Access revoked" : "Revoke failed";

        } else if (path == "/request") {
            t.type      = REQUEST;
            t.requester = params["requester"];
            t.dataHash  = params["dataHash"];
            t.timestamp = time(nullptr);

            bool ok = dataManager.requestAccess(t.requester, t.dataHash);
            block.transactions.push_back(t);
            block.hash = calculateBlockHash(block);
            blockchain.addBlock(block);
            responseBody = ok ? "Access granted to requester" : "Access denied";

        } else {
            responseBody = "Unknown POST endpoint";
        }

    } else {
        responseBody = "Only POST requests are supported";
    }

    string fullResponse = httpResponse(responseBody);
    send(clientSock, fullResponse.c_str(), fullResponse.size(), 0);
    close(clientSock);
}

// Запуск сервера
void startServer(int port) {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(serverSock, 10) < 0) {
        perror("listen");
        exit(1);
    }

    cout << "Server listening on port " << port << "...\n";
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &len);
        if (clientSock < 0) continue;
        thread(processRequest, clientSock).detach();
    }
    close(serverSock);
}

int main() {
    startServer(8080);
    return 0;
}
