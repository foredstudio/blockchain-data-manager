# Blockchain Data Manager

Простий багатопотоковий HTTP‑сервер на C++17 для керування доступом до персональних даних з записом операцій у блокчейн.

## Залежності

- Linux/Unix  
- компілятор C++17 (g++ ≥ 7)  
- pthread  
- стандартні заголовки: `<arpa/inet.h>`, `<netinet/in.h>`, `<unistd.h>` тощо.

## Збірка

```bash
# клонування (якщо не через веб-інтерфейс):
git clone https://github.com/<ваш‑користувач>/blockchain-data-manager.git
cd blockchain-data-manager

# компіляція:
g++ -std=c++17 main.cpp -pthread -o server
