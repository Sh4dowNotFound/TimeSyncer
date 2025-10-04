# ⏳ NtpTimeSyncClient (Windows C++ Precision Time Client)

Jest to klient NTP (Network Time Protocol) w C++ zaprojektowany dla systemu Windows.
Głównym zadaniem aplikacji jest zmierzenie przesunięcia zegara systemowego względem publicznego serwera Google Time (`time.google.com`) i automatyczne skorygowanie lokalnego czasu z użyciem Windows API (SetSystemTime).

## ⚠️ UWAGA DOTYCZĄCA PRECYZJI

Maksymalne opóźnienie 10 nanosekund, choć było początkowym celem, **jest technologicznie nieosiągalne** przy użyciu standardowych kart sieciowych, systemu operacyjnego Windows i protokołu NTP (UDP/IP).
*   **Praktyczna precyzja:** od $0.5 \text{ ms}$ do $100 \text{ ms}$.

## 🚀 Kompilacja i Uruchomienie

### Wymagania
- System Operacyjny: Windows
- Środowisko: Visual Studio 2022 (lub nowsze)
- Wymagany Manifest: Plik `.exe` **MUSI** mieć uprawnienia `requireAdministrator` w pliku manifestu projektu, aby `SetSystemTime` zadziałało!