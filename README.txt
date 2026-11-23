DIGITAL FOOTPRINT ANALYZER

FEATURES
- Live public footprint search using Wikipedia and Hacker News APIs
- Rule-based risk score based on risk indicators; result count alone does not increase risk
- Have I Been Pwned Pwned Passwords check without an API key
- Local password length, character variety, and predictable-pattern checks
- SQLite assessment history
- C++ OOP classes showing abstraction, inheritance, encapsulation, and overriding

REQUIREMENTS
- Windows
- MSYS2 UCRT64
- g++ and SQLite development package
- Internet connection

BUILD
Open MSYS2 UCRT64 in this project folder and run:
  g++ -std=c++17 server.cpp -o server.exe -lsqlite3 -lwinhttp -lws2_32 -lbcrypt
  ./server.exe
Then open http://127.0.0.1:8080 in your browser.

PWNED PASSWORDS
The password check does not require an API key. The password is hashed locally and only the first five characters of its SHA-1 hash are sent to the Pwned Passwords range endpoint. The raw password is not stored.

NOTES
- Wikipedia and Hacker News APIs may return different results; public result count alone does not mean high risk.
- Risk scores are heuristics for educational use and are not proof that an account was hacked.
- Use only your own email or an address you have permission to investigate. Use test passwords for demonstrations.
- Keep index.html, style.css, app.js, server.cpp, and server.exe in the same folder when running the app.


DARK SMOOTH UI
1. Extract the ZIP and open the DigitalFootprintAnalyzer folder.
2. Open MSYS2 UCRT64 in that folder.
3. Compile: g++ -std=c++17 server.cpp -o server.exe -lsqlite3 -lwinhttp -lws2_32 -lbcrypt
4. Run: ./server.exe
5. Open http://127.0.0.1:8080 in your browser. Keep the terminal open.
