#include "server.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET sock_t;
#define MINIGC_CLOSE closesocket
#define MINIGC_SEND_FLAGS 0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define MINIGC_CLOSE close
#ifdef MSG_NOSIGNAL
#define MINIGC_SEND_FLAGS MSG_NOSIGNAL  // a client that hung up must not kill us with SIGPIPE
#else
#define MINIGC_SEND_FLAGS 0
#endif
#endif

namespace {

std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      out += ' ';
    } else if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
               std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

// Returns the value of query parameter `key` (decoded), or "" if absent.
std::string queryParam(const std::string& query, const std::string& key) {
  size_t pos = 0;
  while (pos <= query.size()) {
    size_t amp = query.find('&', pos);
    std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    size_t eq = pair.find('=');
    if (eq != std::string::npos && pair.substr(0, eq) == key) return urlDecode(pair.substr(eq + 1));
    if (amp == std::string::npos) break;
    pos = amp + 1;
  }
  return "";
}

std::string contentType(const std::string& path) {
  auto ends = [&](const char* ext) {
    size_t n = std::strlen(ext);
    return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
  };
  if (ends(".html")) return "text/html; charset=utf-8";
  if (ends(".js")) return "application/javascript; charset=utf-8";
  if (ends(".css")) return "text/css; charset=utf-8";
  if (ends(".json")) return "application/json; charset=utf-8";
  if (ends(".svg")) return "image/svg+xml";
  if (ends(".png")) return "image/png";
  if (ends(".ico")) return "image/x-icon";
  return "application/octet-stream";
}

void sendAll(sock_t c, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int n = send(c, data.data() + sent, static_cast<int>(data.size() - sent), MINIGC_SEND_FLAGS);
    if (n <= 0) return;
    sent += static_cast<size_t>(n);
  }
}

void respond(sock_t c, int status, const std::string& type, const std::string& body) {
  const char* reason = status == 200 ? "OK" : status == 404 ? "Not Found" : status == 400 ? "Bad Request" : "Internal Server Error";
  std::ostringstream h;
  h << "HTTP/1.1 " << status << " " << reason << "\r\nContent-Type: " << type << "\r\nContent-Length: " << body.size()
    << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  sendAll(c, h.str() + body);
}

bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

void handle(sock_t c, minigc::Session& session, const std::string& uiDir) {
  std::string req;
  char buf[4096];
  while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
    int n = recv(c, buf, sizeof buf, 0);
    if (n <= 0) break;
    req.append(buf, static_cast<size_t>(n));
  }
  std::istringstream first(req.substr(0, req.find("\r\n")));
  std::string method, target, version;
  first >> method >> target >> version;
  if (method != "GET" || target.empty()) {
    respond(c, 400, "text/plain", "only GET is supported");
    return;
  }
  std::string path = target, query;
  size_t qm = target.find('?');
  if (qm != std::string::npos) {
    path = target.substr(0, qm);
    query = target.substr(qm + 1);
  }
  path = urlDecode(path);

  try {
    if (path == "/healthz") { respond(c, 200, "text/plain", "ok"); return; }
    if (path == "/api/state") { respond(c, 200, "application/json", session.stateJson()); return; }
    if (path == "/api/demos") { respond(c, 200, "application/json", minigc::Session::demosJson()); return; }
    if (path == "/api/cmd") { respond(c, 200, "application/json", session.runJson(queryParam(query, "c"))); return; }
  } catch (const std::exception& e) {
    respond(c, 500, "text/plain", e.what());
    return;
  }

  if (path == "/") path = "/index.html";
  if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
    respond(c, 400, "text/plain", "bad path");
    return;
  }
  std::string body;
  if (!readFile(uiDir + path, body)) {
    respond(c, 404, "text/plain", "not found: " + path);
    return;
  }
  respond(c, 200, contentType(path), body);
}

bool exists(const std::string& p) { std::ifstream f(p); return static_cast<bool>(f); }

std::string resolveUiDir(const std::string& requested) {
  const char* candidates[] = {"ui", "../ui", "../../ui", "../../../ui"};
  if (!requested.empty() && exists(requested + "/index.html")) return requested;
  for (const char* c : candidates)
    if (exists(std::string(c) + "/index.html")) return c;
  return requested;
}

}  // namespace

int runServer(minigc::Session& session, const std::string& host, int port, const std::string& requestedUiDir) {
  std::string uiDir = resolveUiDir(requestedUiDir);
  if (!exists(uiDir + "/index.html")) {
    std::cerr << "Cannot find the UI directory (looked for '" << uiDir << "/index.html'). Pass --ui <dir>.\n";
    return 1;
  }
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }
#endif
  sock_t server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == INVALID_SOCKET) {
    std::cerr << "Cannot create socket\n";
    return 1;
  }
  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(host.c_str());  // 127.0.0.1 = this machine only, 0.0.0.0 = everywhere
  if (addr.sin_addr.s_addr == INADDR_NONE) {
    std::cerr << "Invalid --host address: " << host << "\n";
    return 1;
  }
  addr.sin_port = htons(static_cast<unsigned short>(port));
  if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(server, 16) != 0) {
    std::cerr << "Cannot listen on " << host << ":" << port << " (is the port already in use? try --port <n>)\n";
    MINIGC_CLOSE(server);
    return 1;
  }
  std::cout << "HeapLens UI:  http://" << (host == "0.0.0.0" ? "127.0.0.1" : host) << ":" << port << "/   (listening on " << host << ")\n"
            << "Serving UI files from: " << uiDir << "\nPress Ctrl+C to stop.\n" << std::flush;

  for (;;) {
    sock_t client = accept(server, nullptr, nullptr);
    if (client == INVALID_SOCKET) continue;
    // a stalled client must not block the single-threaded server forever
#ifdef _WIN32
    DWORD tmo = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof tmo);
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tmo), sizeof tmo);
#else
    timeval tmo;
    tmo.tv_sec = 5;
    tmo.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof tmo);
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof tmo);
#endif
    try {
      handle(client, session, uiDir);
    } catch (const std::exception& e) {
      std::cerr << "request failed: " << e.what() << "\n";
    }
    MINIGC_CLOSE(client);
  }
}
