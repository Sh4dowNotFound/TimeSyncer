#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <cmath> 
#include <limits> 
#include <stdio.h> // Wymagany dla prostego buforowania/I/O

// --- Sekcja Windows/Winsock ---
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h> 

#pragma comment(lib, "ws2_32.lib") 
// ------------------------------

// LINIA ROZWIĄZUJĄCA BŁĘDY E0020 (dla cout, cin, endl, etc.)
using namespace std;

// MAKRA
#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define NTP_TIMESTAMP_DELTA 2208988800ULL


// ====================================================================
// PROTOTYPY FUNKCJI (dla VS)
// ====================================================================

static bool enable_system_time_privilege();
static int attempt_to_synchronize_system_time(long long offset_nanoseconds);
static int get_ntp_server_addr(const string& hostname, sockaddr_in& addr);
static std::chrono::nanoseconds ntp_to_nanoseconds(uint32_t seconds, uint32_t fraction);
static int sync_and_calculate_offset(const string& server_name);


// ====================================================================
// DEFINICJE FUNKCJI WINDOWS API
// ====================================================================

static bool enable_system_time_privilege() {
    HANDLE token = NULL;
    TOKEN_PRIVILEGES privileges;
    LUID luid;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) { return false; }
    if (!LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &luid)) {
        if (token) CloseHandle(token);
        return false;
    }

    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        if (token) CloseHandle(token);
        return false;
    }

    CloseHandle(token);
    return true;
}

static int attempt_to_synchronize_system_time(long long offset_nanoseconds) {
    if (abs(offset_nanoseconds) < 5000000) {
        cout << "\nINFO: Offset is too small (abs < 5 ms). Time not set to avoid unnecessary clock jump." << endl;
        return 0;
    }

    long long offset_ms = offset_nanoseconds / 1000000;
    cout << "Attempting to correct time. Offset: " << offset_ms << " ms" << endl;

    if (!enable_system_time_privilege()) {
        cerr << "\nERROR: Could not get SE_SYSTEMTIME_NAME privilege." << endl;
        cerr << "Program MUST be executed with Administrator rights to change system time." << endl;
        return -1;
    }
    cout << "Successfully obtained SET_SYSTEM_TIME privilege." << endl;

    // Krok 2: Obliczenie nowego czasu
    SYSTEMTIME st_now;
    GetSystemTime(&st_now);

    FILETIME ft_now;
    SystemTimeToFileTime(&st_now, &ft_now);

    ULARGE_INTEGER uli;
    uli.LowPart = ft_now.dwLowDateTime;
    uli.HighPart = ft_now.dwHighDateTime;

    long long adjustment_100ns = offset_nanoseconds / 100;
    uli.QuadPart += adjustment_100ns;

    ft_now.dwLowDateTime = uli.LowPart;
    ft_now.dwHighDateTime = uli.HighPart;

    SYSTEMTIME st_corrected;
    FileTimeToSystemTime(&ft_now, &st_corrected);

    // Krok 3: Ustaw czas systemowy
    if (!SetSystemTime(&st_corrected)) {
        cerr << "\nFATAL ERROR: SetSystemTime failed! Windows API Error: " << GetLastError() << endl;
        return -2;
    }
    cout << "\nSUCCESS: System time was corrected by: " << offset_ms << " ms." << endl;

    return 0;
}


// ====================================================================
// DEFINICJE FUNKCJI LOGIKI APLIKACJI
// ====================================================================

static int get_ntp_server_addr(const string& hostname, sockaddr_in& addr) {
    struct addrinfo* result = nullptr, * ptr = nullptr, hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;

    int res = getaddrinfo(hostname.c_str(), to_string(NTP_PORT).c_str(), &hints, &result);
    if (res != 0) {
        cerr << "Error: Cannot resolve hostname " << hostname << ". getaddrinfo failed with error: " << res << endl;
        return -1;
    }

    ptr = result;
    if (ptr != nullptr) {
        memcpy(&addr, ptr->ai_addr, ptr->ai_addrlen);
    }
    else {
        cerr << "Error: No suitable address found." << endl;
        freeaddrinfo(result);
        return -1;
    }

    freeaddrinfo(result);
    return 0;
}

static std::chrono::nanoseconds ntp_to_nanoseconds(uint32_t seconds, uint32_t fraction) {
    uint64_t full_seconds = static_cast<uint64_t>(ntohl(seconds));
    auto sec_duration = std::chrono::seconds(full_seconds);

    uint32_t net_fraction = ntohl(fraction);

    double fraction_sec = (double)net_fraction / 4294967296.0;

    auto ns_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(fraction_sec)
    );

    return std::chrono::nanoseconds(sec_duration) + ns_duration;
}

static int sync_and_calculate_offset(const string& server_name) {
    SOCKET sockfd; sockaddr_in servaddr; char ntp_packet[NTP_PACKET_SIZE] = { 0 };
    ntp_packet[0] = 0x1B;

    if (get_ntp_server_addr(server_name, servaddr) == -1) return -1;
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET) { cerr << "Error creating socket" << endl; return -1; }

    int timeout_ms = 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    auto t1_local_tp = std::chrono::system_clock::now();
    if (sendto(sockfd, ntp_packet, NTP_PACKET_SIZE, 0, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        cerr << "Error sending NTP request. WSA Error: " << WSAGetLastError() << endl;
        closesocket(sockfd); return -1;
    }

    socklen_t servlen = sizeof(servaddr);
    if (recvfrom(sockfd, ntp_packet, NTP_PACKET_SIZE, 0, (struct sockaddr*)&servaddr, &servlen) < 0) {
        cerr << "Error receiving NTP response. WSA Error: " << WSAGetLastError() << " (Possibly timeout)" << endl;
        closesocket(sockfd); return -1;
    }

    auto t4_local_tp = std::chrono::system_clock::now();
    closesocket(sockfd);

    uint32_t transmit_seconds = *(uint32_t*)&ntp_packet[40];
    uint32_t transmit_fraction = *(uint32_t*)&ntp_packet[44];

    auto t3_remote_dur = ntp_to_nanoseconds(transmit_seconds, transmit_fraction)
        - std::chrono::nanoseconds(NTP_TIMESTAMP_DELTA * 1000000000ULL);

    auto epoch_tp = std::chrono::system_clock::from_time_t(0);
    auto t1_local_dur = t1_local_tp - epoch_tp;
    auto t4_local_dur = t4_local_tp - epoch_tp;

    auto offset_nanoseconds = t3_remote_dur - ((t1_local_dur + t4_local_dur) / 2);
    auto rtt_nanoseconds = t4_local_dur - t1_local_dur;

    long long final_offset_ns = offset_nanoseconds.count();

    // 8. Wyświetlenie Wyników
    cout << "Synchronization attempt with: " << server_name << " (Google Public NTP)" << endl;
    cout << fixed << setprecision(6);
    cout << "--------------------------------------------------------" << endl;

    cout << "Calculated Clock Offset: ";
    cout << setw(10) << (double)final_offset_ns / 1000000.0 << " ms" << endl;

    cout << "Network Round-Trip Delay (RTT): ";
    cout << setw(10) << (double)rtt_nanoseconds.count() / 1000000.0 << " ms" << endl;

    cout << "--------------------------------------------------------" << endl;
    cerr << "\n*** ATTENTION: The required precision of 10 nanoseconds IS NOT ACHIEVED." << endl;
    cerr << "*** This measurement shows the practical limit of NTP synchronization, which is usually in milliseconds or at best microseconds." << endl;

    return attempt_to_synchronize_system_time(final_offset_ns);
}


// ====================================================================
// GŁÓWNA FUNKCJA (MAIN) Z PAUZĄ
// ====================================================================

int main() {
    // Inicjalizacja Winsock (Windows)
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        cerr << "WSAStartup failed with error: " << iResult << endl;
        return 1;
    }

    int result = sync_and_calculate_offset("time.google.com");

    // Czyszczenie Winsock (Windows)
    WSACleanup();

    // --- BLOK PAUZUJĄCY (Maksymalnie Uproszczony) ---
    cout << "\nOperation finished." << endl;
    cout << "Press ENTER to close this window..." << endl;

    // Uproszczone oczekiwanie
    cin.clear();
    cin.ignore(10000, '\n');
    cin.get();
    // ------------------------------------------

    return result;
}